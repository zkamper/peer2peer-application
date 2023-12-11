#include "common.h"

#define NUM_THREADS 5

vector<sockaddr_in> peers;
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

void *handlePeer(void *arg){
    Requests request;
    int peer_fd = *(int*)arg;
    // Handle Request
    read(peer_fd,&request,sizeof(request));
    int peers_count = peers.size();
    int files_count;
    unordered_map<string,File> files;
    switch(request){
        case T_ONLINE:
            sockaddr_in peer_addr;
            read(peer_fd,&peer_addr,sizeof(peer_addr));
            pthread_mutex_lock(&mutex);
            peers.push_back(peer_addr);
            pthread_mutex_unlock(&mutex);
            request = T_ACCEPTED;
            if(write(peer_fd,&request,sizeof(request)) < 0){
                printError("error while writing to peer");
                return nullptr;
            }
            cout<<TRACKER<<"Peer connected at "<<getAddressReadable(peer_addr)<<endl;
            break;
        case T_GETPEERS:
            cout << TRACKER << "Peers request from " << getAddressReadable(peer_addr) << endl;
            if(write(peer_fd,&peers_count,sizeof(peers_count)) < 0){
                printError("error while writing to peer");
                return nullptr;
            }
            for(int i = 0; i < peers_count; i++){
                if(write(peer_fd,&peers[i],sizeof(peers[i])) < 0){
                    printError("error while writing to peer");
                    return nullptr;
                }
            }
            break;
        case T_OFFLINE:
            if(read(peer_fd,&peer_addr,sizeof(peer_addr)) < 0){
                printError("error while reading from peer");
                return nullptr;
            }
            cout<<TRACKER<<"Peer disconnected at "<<getAddressReadable(peer_addr)<<endl;
            for(int i = 0; i < peers_count; i++){
                if(peers[i].sin_addr.s_addr == peer_addr.sin_addr.s_addr && peers[i].sin_port == peer_addr.sin_port){
                    peers.erase(peers.begin()+i);
                    break;
                }
            }
            break;
        case T_GETFILES:
            cout<<TRACKER<<"Files request from "<<getAddressReadable(peer_addr)<<endl;
            for(int i = 0; i < peers_count; i++){
                int other_peer = socket(AF_INET,SOCK_STREAM,0);
                if(other_peer < 0){
                    printError("socket error");
                    return nullptr;
                }
                if(connect(other_peer,(struct sockaddr*)&peers[i],sizeof(peers[i])) < 0){
                    printError("couldn't connect to peer to request files");
                    return nullptr;
                }
                request = T_GETFILES;
                // Trimitem request-ul
                if( write(other_peer,&request,sizeof(request)) < 0){
                    printError("error while writing to peer");
                    return nullptr;
                }
                // Primim nr + proprietatile fisierelor
                if( read(other_peer,&files_count,sizeof(files_count)) < 0){
                    printError("error while reading from peer");
                    return nullptr;
                }
                for(int i = 0; i < files_count; i++){
                    File file;
                    memset(&file,0,sizeof(file));
                    if( read(other_peer,&file,sizeof(file)) < 0){
                        printError("error while reading from peer");
                        return nullptr;
                    }
                    files[file.hash] = file;
                }
                close(other_peer);
            }
            // Trimitem nr + proprietatile fisierelor
            files_count = files.size();
            if( write(peer_fd,&files_count,sizeof(files_count)) < 0){
                printError("error while writing to peer file count");
                return nullptr;
            }
            for(auto it = files.begin(); it != files.end(); it++){
                if( write(peer_fd,&it->second,sizeof(it->second)) < 0){
                    printError("error while writing to peer file data");
                    return nullptr;
                }
            }
            break;
        default:
            break;
    }
    return nullptr;
}

int sock_fd;

void handle_sig(int sig){
    cout << endl << "Tracker stopped" << endl;
    close(sock_fd);
    exit(0);
}

int main(){
    sockaddr_in tracker_addr, peer_addr;
    pthread_t thread_id;
    
    // Override la SIGINT si SIGQUIT pentru a inchide tracker-ul
    signal(SIGQUIT,handle_sig);
    signal(SIGINT,handle_sig);

    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0){
        printError("socket error");
        return -1;
    }

    int sock_opt = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &sock_opt, sizeof(sock_opt));

    memset(&tracker_addr, 0, sizeof(tracker_addr));
    tracker_addr.sin_family = AF_INET;
    tracker_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    tracker_addr.sin_port = htons(PORT_TRACKER);

    if (bind(sock_fd, (struct sockaddr *)&tracker_addr, sizeof(tracker_addr)) < 0){
        char err_msg[256];
        sprintf(err_msg, "bind error at port %d", htons(tracker_addr.sin_port));
        printError(err_msg);
        return -1;
    }
    if (listen(sock_fd, 10) < 0){
        printError("error while listening for peers");
        return -1;
    }
    cout<< TRACKER << "Started at address "<< getAddressReadable(tracker_addr) << endl;
    while (true){
        socklen_t peer_addr_len = sizeof(peer_addr);
        int peer_fd = acceptConnection(sock_fd, peer_addr, peer_addr_len);
        if (peer_fd < 0){
            printError("error while accepting connection from peer");
            return -1;
        }
        
        int *p_peer_fd = new int;
        *p_peer_fd = peer_fd;

        if(pthread_create(&thread_id,NULL,handlePeer,(void*)p_peer_fd) < 0){
            printError("error while creating thread");
            free(p_peer_fd);
            return -1;
        }

        pthread_detach(thread_id);
    }
}
