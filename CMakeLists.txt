cmake_minimum_required(VERSION 3.20)
project(zos_semestralka C)

set(CMAKE_C_STANDARD 99)

add_executable(zos_semestralka main.c cmds.c cmds.h cmd_handler.c cmd_handler.h fs.c fs.h util.h util.c io.c io.h)
