#pragma once

typedef void(*Logger)(unsigned level, const char* msg, ...);

bool e57Parser(Logger logger, const char* path, const char* ptr, size_t size);
