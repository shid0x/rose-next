#ifndef _ROSE_RML_SYSTEM_H_
#define _ROSE_RML_SYSTEM_H_

/**
 * Rml::SystemInterface for the Rose client: clock, logging and clipboard.
 *
 * The clock is deliberately NOT g_GameDATA.GetGameTime(): that is built on
 * timeGetTime(), whose quantisation is the documented cause of the client's
 * historic frame-pacing jitter ( see client CLAUDE.md, "Frame Timing & Timer
 * Precision" ). RmlUi drives CSS transitions and animations off this value, so
 * it uses QueryPerformanceCounter directly and stays smooth regardless.
 */

#include <RmlUi/Core/SystemInterface.h>

class RoseRmlSystem: public Rml::SystemInterface {
public:
    RoseRmlSystem();

    virtual double GetElapsedTime();
    virtual bool LogMessage(Rml::Log::Type type, const Rml::String& message);
    virtual void SetClipboardText(const Rml::String& text);
    virtual void GetClipboardText(Rml::String& text);

private:
    double m_dFrequency;
    long long m_llStart;
};

#endif /// _ROSE_RML_SYSTEM_H_
