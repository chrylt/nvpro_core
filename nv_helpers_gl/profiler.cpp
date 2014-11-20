/*-----------------------------------------------------------------------
  Copyright (c) 2014, NVIDIA. All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:
   * Redistributions of source code must retain the above copyright
     notice, this list of conditions and the following disclaimer.
   * Neither the name of its contributors may be used to endorse 
     or promote products derived from this software without specific
     prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
  EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
  PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
  CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
  OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
-----------------------------------------------------------------------*/

#include "profiler.hpp"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX 
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif


//////////////////////////////////////////////////////////////////////////

namespace nv_helpers_gl
{

  Profiler::Profiler()
    : m_frameEntries(0)
    , m_numFrames(0)
    , m_resetDelay(0)
    , m_frequency(1)
    , m_level(0)
  {

  }

  void Profiler::beginFrame()
  {
    m_frameEntries = 0;
    m_level = 0;
  }

  void Profiler::endFrame()
  {
    if (m_frameEntries != m_lastEntries) {
      m_lastEntries = m_frameEntries;
      m_resetDelay = CONFIG_DELAY;
    }

    if (m_resetDelay) {
      m_resetDelay--;
      for (size_t i = 0; i < m_entries.size(); i++){
        
        m_entries[i].numTimes = 0;
        m_entries[i].cpuTimes = 0;
        m_entries[i].gpuTimes = 0;
      }
      m_numFrames = 0;
    }

    if (m_numFrames > FRAME_DELAY){
      for (size_t i = 0; i < m_frameEntries; i++){
        Entry& entry = m_entries[i];

        if (entry.splitter) continue;

        GLint available = 0;
        GLuint queryFrame = (m_numFrames + 1) % FRAME_DELAY;
        glGetQueryObjectiv(entry.queries[queryFrame + FRAME_DELAY], GL_QUERY_RESULT_AVAILABLE,&available);

        if (available) {
          GLuint64 beginTime;
          GLuint64 endTime;
          glGetQueryObjectui64v(entry.queries[queryFrame], GL_QUERY_RESULT,&beginTime);
          glGetQueryObjectui64v(entry.queries[queryFrame + FRAME_DELAY], GL_QUERY_RESULT,&endTime);

          // nanoseconds to microseconds
          GLuint64 gpuNano = endTime - beginTime;
          double gpu = double(gpuNano) / 1000.0;
          entry.gpuTimes += gpu;
          entry.cpuTimes += entry.deltas[queryFrame];
          entry.numTimes ++;
        }
      }
    }

    m_numFrames++;
  }


  void Profiler::grow(size_t newsize)
  {
    size_t oldsize = m_entries.size();
    assert(newsize > oldsize);

    m_entries.resize(newsize);
    for (size_t i = oldsize; i < newsize; i++){
      Entry &entry = m_entries[i];
      glGenQueries(2 * FRAME_DELAY, &entry.queries[0]);
      entry.name = NULL;
    }
  }

  void Profiler::init()
  {
#ifdef _WIN32
    LARGE_INTEGER sysfrequency;
    if (QueryPerformanceFrequency(&sysfrequency)){
      m_frequency = (double)sysfrequency.QuadPart;
    }
    else{
      m_frequency = 1;
    }
#endif

    grow(START_SECTIONS);
  }

  void Profiler::deinit()
  {
    for (size_t i = 0; i < m_entries.size(); i++){
      Entry &entry = m_entries[i];
      glDeleteQueries(2 * FRAME_DELAY, &entry.queries[0]);
      entry.name = NULL;
    }
  }

  void Profiler::reset(int delay)
  {
    m_resetDelay = delay;
  }

  std::string format(const char* msg, ...)
  {
    std::size_t const STRING_BUFFER(8192);
    char text[STRING_BUFFER];
    va_list list;

    if(msg == 0)
      return std::string();

    va_start(list, msg);
    vsprintf(text, msg, list);
    va_end(list);

    return std::string(text);
  }

  void Profiler::print( std::string &stats)
  {
    stats.clear();

    bool hadtimers = false;

    for (size_t i = 0; i < m_lastEntries; i++){
      Entry &entry = m_entries[i];
      entry.accumulated = false;
    }

    for (size_t i = 0; i < m_lastEntries; i++){
      static const char* spaces = "        "; // 8
      Entry &entry = m_entries[i];
      int level = 7 - (entry.level > 7 ? 7 : entry.level);

      if (!entry.numTimes || entry.accumulated) continue;

      hadtimers = true;

      double gpu = entry.gpuTimes/entry.numTimes;
      double cpu = entry.cpuTimes/entry.numTimes;
      bool found = false;
      for (size_t n = i+1; n < m_lastEntries; n++){
        Entry &otherentry = m_entries[n];
        if (otherentry.name == entry.name && 
          otherentry.level == entry.level &&
          !otherentry.accumulated
          )
        {
          found = true;
          gpu += otherentry.gpuTimes/otherentry.numTimes;
          cpu += otherentry.cpuTimes/otherentry.numTimes;
          otherentry.accumulated = true;
        }

        if (otherentry.splitter && otherentry.level <= entry.level) break;
      }

      if (found){
        stats += format("%sTimer %s;\t GL %6d; CPU %6d; (microseconds, accumulated loop)\n",&spaces[level], entry.name, (unsigned int)(gpu), (unsigned int)(cpu));
      }
      else {
        stats += format("%sTimer %s;\t GL %6d; CPU %6d; (microseconds, avg %d)\n",&spaces[level],entry.name, (unsigned int)(gpu), (unsigned int)(cpu), (unsigned int)entry.numTimes);
      }
    }
  }

  double Profiler::getMicroSeconds()
  {
#ifdef _WIN32
    LARGE_INTEGER time;
    if (QueryPerformanceCounter(&time)){
      return (double(time.QuadPart) / m_frequency)*1000000.0;
    }
#endif
    return 0;
  }

  Profiler::FrameHelper::FrameHelper( Profiler& profiler, double curtime, double printInterval, std::string& stats )
    : m_profiler(profiler)
    , m_stats(stats)
  {
    m_print = ((curtime - m_profiler.m_lastPrint) > printInterval);
    if (m_print){
      m_profiler.m_lastPrint = curtime;
    }
    m_profiler.beginFrame();
  }

  Profiler::FrameHelper::~FrameHelper()
  {
    m_profiler.endFrame();
    if (m_print){
      m_profiler.print(m_stats);
      m_profiler.reset(1);
    }
  }

}

