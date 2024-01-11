#pragma once

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <cstdio>
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
#include <fcntl.h>
#include <fstream>
#include <sys/ioctl.h>

#define PORT 8000
#define MAX_PORT 8256       // Portul maxim pentru a asocia unui peer
#define PORT_TRACKER 7999   // Port unic pentru tracker
#define CHUNK_SIZE 1024     // Dimensiunea unui chunk

#define ERROR "\033[31mERROR: \033[0m"
#define TRACKER "\033[34m[ TRACKER ] \033[0m"

using namespace std;

enum Requests{
    T_ONLINE,
    T_ACCEPTED,
    T_OFFLINE,
    T_GETFILES,
    T_GETPEERS,
    T_GETFILE,
    P_GETFILE
};

struct File{
    char name[256];     // Numele fisierului
    char hash[65];     // Hash-ul fisierului
    char extension[10]; // Extensia fisierului
    int size;           // Dimensiunea fisierului
};

struct Options{
    char files_path[256];   // Calea către directorul cu fișiere pentru a fi incarcate in retea (default se va folosi ./downloads)
};

void printError(const char* msg) { char msg_err[256]; sprintf(msg_err, "%s%s", ERROR, msg); perror(msg_err); }

char* getAddressReadable(sockaddr_in addr){
    char* addr_readable = new char[256];
    sprintf(addr_readable, "%s:%d", inet_ntoa(addr.sin_addr), htons(addr.sin_port));
    return addr_readable;
}

const char* genHeaderClient(const char* msg){
    winsize size;
    ioctl(STDOUT_FILENO,TIOCGWINSZ,&size);
    int width = size.ws_col;
    char* header_top = new char[width];
    char* header_msg = new char[width];
    memset(header_top,0,width);
    memset(header_msg,0,width);
    header_top[0] = '+';
    header_top[width-1] = '+';
    header_msg[0] = '|';
    header_msg[width-1] = '|';
    for(int i = 1 ; i < width - 1 ; i ++){
        header_top[i] = '-';
        header_msg[i] = ' ';
    }
    int start = (width - strlen(msg))/2;
    int finish = start + strlen(msg);
    for(int i = start; i < finish; i++){
        header_msg[i] = msg[i-start];
    }
    char* header = new char[width*3];
    memset(header,0,width*3);
    sprintf(header,"%s\n%s\n%s",header_top,header_msg,header_top);
    return header;
}

// Returns 1 if file is found, 0 otherwise
int searchFile(char* path, char* hash){
    DIR* dir;
    struct dirent *ent;
    dir = opendir(path);
    if(dir == nullptr){
        printError("error while opening directory");
        return -1;
    }
    while((ent = readdir(dir)) != nullptr){
        if(ent->d_type != DT_REG)
            continue;
        char file_path[512];
        strcpy(file_path,path);
        strcat(file_path,"/");
        strcat(file_path,ent->d_name);
        char command[600];
        strcpy(command,"sha256sum \"");
        strcat(command,file_path);
        strcat(command,"\"");
        FILE* fp = popen(command,"r");
        if(fp == nullptr){
            printError("error while getting file hash");
        }
        char other_hash[65];
        fgets(other_hash,64,fp);
        if(strcmp(hash,other_hash) == 0){
            return 1;
        }
    }
    closedir(dir);
    return 0;
}

void searchFileName(char* name, char* path,char* hash){
    DIR* dir;
    struct dirent *ent;
    dir = opendir(path);
    if(dir == nullptr){
        printError("error while opening directory");
        return;
    }
    while((ent = readdir(dir)) != nullptr){
        if(ent->d_type != DT_REG)
            continue;
        char file_path[512];
        strcpy(file_path,path);
        strcat(file_path,"/");
        strcat(file_path,ent->d_name);
        char command[600];
        strcpy(command,"sha256sum \"");
        strcat(command,file_path);
        strcat(command,"\"");
        FILE* fp = popen(command,"r");
        if(fp == nullptr){
            printError("error while getting file hash");
        }
        char other_hash[65];
        fgets(other_hash,64,fp);
        if(strcmp(hash,other_hash) == 0){
            strcpy(name,ent->d_name);
            closedir(dir);
            return;
        }
    }
    closedir(dir);
    return;
}

void getFileChunk(int other_peer, char *file_name, int offset, int chunk_size, FILE *file_fd){
    Requests ping = P_GETFILE;
    // Request P_GET FILE - file name - offset - chunk size
    send(other_peer, &ping, sizeof(ping), 0);
    char name[256];
    memset(name, 0, sizeof(name));
    strcpy(name, file_name);
    send(other_peer, name, 256, 0);
    send(other_peer, &offset, sizeof(offset), 0);
    send(other_peer, &chunk_size, sizeof(chunk_size), 0);
    // Receive chunk
    char chunk[CHUNK_SIZE];
    memset(chunk, 0, sizeof(chunk));
    read(other_peer, chunk, sizeof(chunk));
    fseek(file_fd, offset, SEEK_SET);
    fwrite(chunk, sizeof(char), chunk_size, file_fd);
    printf("Received chunk: %s\n", chunk);
    close(other_peer);
}

int getFiles(char* path, vector<File> &files){
    DIR *dir;
    struct dirent *ent;
    dir = opendir(path);
    if(dir == nullptr){
        printError("error while opening directory");
        return -1;
    }
    while((ent = readdir(dir)) != nullptr){
        if(ent->d_type != DT_REG)
            continue;
        File file;
        memset(&file,0,sizeof(file));
        strcpy(file.name,ent->d_name);
        char extension[10];
        strcpy(extension,strchr(ent->d_name,'.')+1);
        strcpy(file.extension,extension);
        char file_path[512];
        strcpy(file_path,path);
        strcat(file_path,"/");
        strcat(file_path,ent->d_name);
        struct stat file_stats;
        if(stat(file_path,&file_stats) < 0){
            printError("error while getting file stats");
            return -1;
        }
        file.size = file_stats.st_size;
        char command[600];
        strcpy(command,"sha256sum \"");
        strcat(command,file_path);
        strcat(command,"\"");
        FILE* fp = popen(command,"r");
        if(fp == nullptr){
            printError("error while getting file hash");
        }
        fgets(file.hash,64,fp);
        files.push_back(file);
    }
    closedir(dir);
    int files_count = files.size();
    return files_count;
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
