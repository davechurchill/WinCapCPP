#pragma once

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

class Logger
{
    std::string   m_logFileName = "log.txt";
    std::ofstream m_fout;

    Logger()
    {
        m_fout.open(m_logFileName, std::ios::out | std::ios::binary);
        if (!m_fout.is_open())
        {
            std::cerr << "Failed to open log file: " << m_logFileName << '\n';
        }
    }

    ~Logger()
    {
        if (m_fout.is_open())
        {
            m_fout.flush();
        }
    }

public:

    static Logger& Instance()
    {
        static Logger instance;
        return instance;
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void writeToLog(const std::string& message)
    {
        if (!m_fout.is_open()) { return; }
        m_fout << message;
        m_fout.flush();
    }

    Logger& operator<<(const char* message)
    {
        writeToLog(message ? std::string(message) : std::string());
        return *this;
    }

    Logger& operator<<(const std::string& message)
    {
        writeToLog(message);
        return *this;
    }

    template<typename T>
    Logger& operator<<(const T& message)
    {
        std::ostringstream oss;
        oss << message;
        writeToLog(oss.str());
        return *this;
    }
};

inline Logger& CapLog()
{
    return Logger::Instance();
}
