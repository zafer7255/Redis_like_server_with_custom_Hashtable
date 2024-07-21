Redis-like server project:

Redis-like Server in C++
This project is a basic implementation of a Redis-like server using C++. It supports core operations similar to Redis, including GET, SET, and DEL. The server is designed to handle multiple client connections efficiently using socket programming and non-blocking I/O.

Features
Basic Operations: Implements GET, SET, and DEL commands.
Non-Blocking I/O: Ensures the server remains responsive under high load.
Custom Hashtable: Utilizes a custom hashtable for efficient key-value storage.
Memory Management: Includes careful handling of memory to prevent leaks.

Prerequisites
C++11 or later
Basic knowledge of socket programming and data structures
