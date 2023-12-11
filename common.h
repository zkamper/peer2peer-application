#pragma once

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <string.h>
#include <sys/wait.h>
#include <iostream>
#include <pthread.h>
#include <stack>
#include <vector>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unordered_map>

#define PORT 8000
#define MAX_PORT 8256   // Portul maxim pentru a asocia unui peer
#define PORT_TRACKER 7999 // Port unic pentru tracker

#define ERROR "\033[31mERROR: \033[0m"
#define TRACKER "\033[34m[ TRACKER ] \033[0m"

using namespace std;

enum Requests{
    T_ONLINE,
    T_ACCEPTED,
    T_OFFLINE,
    T_GETFILES,
    T_GETPEERS,
};

struct File{
    char name[128];     // Numele fisierului
    char hash[256];     // Hash-ul fisierului
    char extension[10]; // Extensia fisierului
    int size;           // Dimensiunea fisierului
};

struct Options{
    char files_path[256];   // Calea către directorul cu fișiere pentru a fi incarcate in retea (default se va folosi ./p2p_files)
};

void printError(const char* msg) { char msg_err[256]; sprintf(msg_err, "%s%s", ERROR, msg); perror(msg_err); }

char* getAddressReadable(sockaddr_in addr){
    char* addr_readable = new char[256];
    sprintf(addr_readable, "%s:%d", inet_ntoa(addr.sin_addr), htons(addr.sin_port));
    return addr_readable;
}

int bindSocket(int &sock_fd, sockaddr_in &server_addr){
    //We initiate TCP socket
    sock_fd = socket(AF_INET,SOCK_STREAM,0);
    if(sock_fd < 0){
        printError("socket error");
        return -1;
    }
    
    int sock_opt = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &sock_opt, sizeof(sock_opt));

    memset(&server_addr,0,sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    // Dacă sunt mai multe instante ale aplicatiei pe acelasi host, aloca alt port
    for(int i = PORT;i < MAX_PORT;i++){
        server_addr.sin_port = htons(i);
        if(bind(sock_fd,(struct sockaddr*)&server_addr,sizeof(server_addr)) == 0){
            return 0;
        }
    }
    return -1;
}


int connectToTracker(sockaddr_in &tracker_addr){
    int tracker_fd = socket(AF_INET,SOCK_STREAM,0);
    if(tracker_fd<0){
        printError("error while creating socket");
        return -1;
    }

    memset(&tracker_addr,0,sizeof(tracker_addr));
    tracker_addr.sin_family = AF_INET;
    tracker_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    tracker_addr.sin_port = htons(PORT_TRACKER);
    
    if(connect(tracker_fd,(struct sockaddr*)&tracker_addr,sizeof(tracker_addr))<0){
        printError("tracker appears to be offline");
        return -1;
    }

    return tracker_fd;
}

int acceptConnection(int sock_fd, sockaddr_in &peer_addr, socklen_t &peer_addr_len){
    int peer_fd = accept(sock_fd,(struct sockaddr*)&peer_addr,&peer_addr_len);
    return peer_fd;
}
