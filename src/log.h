#pragma once

void LogInit(const char* path);
void LogClose();
void LogPrintf(const char* fmt, ...);
