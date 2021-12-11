cmake_minimum_required(VERSION 3.20)
project(zos_semestralka C)

set(CMAKE_C_STANDARD 11)

add_executable(zos_semestralka main.c cmds.c cmds.h cmd_handler.c cmd_handler.h consts.h fs.c fs.h util.h util.c)
