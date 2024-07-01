# Multi-threaded HTTP Server
Evan Luu \
Spring 2023

## httpserver.c
This program takes input two commands, get or put, and a filename. If it takes a put command, it also needs file contents after the filename. 

To run httpserver.c, use the command line \
./httpserver <portnumber>
followed by a command that looks like this \
printf "PUT /stink.txt HTTP/1.1\r\nContent-Length: 5\r\nRequest-Id: 2\r\n\r\n54321" | nc localhost 1234


### files
 - httpserver.c
 - Makefile
 - README.md
 - asgn4_helper_funcs.a
 - asgn4_helper_funcs.h
 - connection.h
 - debug.h
 - queue.h
 - request.h
 - response.h