#include "common.h"

int sock_fd,tracker_fd;
sockaddr_in server_addr;
pid_t pid;
Options options;

void handle_sig(int sig){
    cout << endl << "Exiting..." << endl;
    Requests ping = T_OFFLINE;
    send(tracker_fd,&ping,sizeof(T_OFFLINE), 0);
    send(tracker_fd,&server_addr,sizeof(server_addr), 0);
    close(tracker_fd);
    if(sig != SIGUSR1)
        kill(pid,SIGKILL);
    exit(0);
}

void initOptions(){
    strcpy(options.files_path,"./downloads");
    if(opendir(options.files_path) == nullptr){
        mkdir(options.files_path,0777);
    }
    else{
        closedir(opendir(options.files_path));
    }
}

int main()
{
    // Init phase - initializam peer-ul + conexiunea la tracker
    
    initOptions();

    if (bindSocket(sock_fd, server_addr) < 0)
    {
        char err_msg[256];
        sprintf(err_msg, "bind error at port %d", ntohs(server_addr.sin_port));
        printError(err_msg);
        return -1;
    }
    
    if (listen(sock_fd, 10) < 0)
    {
        printError("error while listening for peers");
        return -1;
    }
    // Ping la tracker
    sockaddr_in tracker_addr;
    tracker_fd = connectToTracker(tracker_addr);
    if (tracker_fd < 0){
        return -1;
    }

    Requests ping = T_ONLINE;

    if (send(tracker_fd, &ping, sizeof(ping), 0) < 0){
        printError("tracker appears to be offline");
        return -1;
    }
    if (send(tracker_fd, &server_addr, sizeof(server_addr), 0) < 0){
        printError("error while sending server address to tracker");
        return -1;
    }
    if (read(tracker_fd, &ping, sizeof(ping)) < 0){
        printError("error while reading tracker response");
        return -1;
    }
    if (ping != T_ACCEPTED){
        printError("connection to tracker rejected");
        return -1;
    }
    cout << "[ " << inet_ntoa(server_addr.sin_addr) << " ] Server started at port " << ntohs(server_addr.sin_port) << endl;

/*
    Fork ce are rolul de a separa programul in componenta ce accepta conexiuni (server)
    si componenta ce se conecteaza la alti peers (client)
*/
    system("clear");
    
    if ((pid = fork())){
        // Parent - client code
        signal(SIGQUIT, handle_sig);
        signal(SIGINT, handle_sig);
        signal(SIGUSR1, handle_sig); // Semnal special pentru a opri clientul din copil
        close(sock_fd);
        while(true){
            close(tracker_fd);
            tracker_fd = connectToTracker(tracker_addr);
            if (tracker_fd < 0){
                return -1;
            }
            cout<<"\nEnter number of action you wish to execute: \n\t (1) Get current peers\n\t (2) Get files on the network\n\t (3) Exit\n\nCommand: ";
            char option[2];
            scanf("%s",option);
            int action = atoi(option);
            Requests ping;
            sockaddr_in other_peer_addr;
            int peers_count;
            int files_count;
            vector<File> files;
            switch(action){
                case 1:
                    // Get current peers
                    ping = T_GETPEERS;
                    cout<<"Getting peers...\n";
                    if (send(tracker_fd, &ping, sizeof(ping), 0) < 0){
                        printError("error while sending request to tracker");
                        return -1;
                    }
                    if (read(tracker_fd, &peers_count, sizeof(peers_count)) < 0){
                        printError("error while reading tracker response");
                        return -1;
                    }
                    cout<<"There are "<<peers_count<<" peers online\n";
                    for(int i = 0; i < peers_count; i++){
                        if (read(tracker_fd, &other_peer_addr, sizeof(other_peer_addr)) < 0){
                            printError("error while reading tracker response");
                            return -1;
                        }
                        cout<<"Peer "<<i+1<<" connected at "<<getAddressReadable(other_peer_addr)<<endl;
                    }
                    break;
                case 2:
                    ping = T_GETFILES;
                    cout<<"Getting files...\n";
                    if (send(tracker_fd, &ping, sizeof(ping), 0) < 0){
                        printError("error while sending request to tracker");
                        return -1;
                    }
                    if (read(tracker_fd, &files_count, sizeof(files_count)) < 0){
                        printError("error while reading tracker response");
                        return -1;
                    }
                    cout<<"There are "<<files_count<<" files on the network\n";
                    for(int i = 0; i < files_count; i++){
                        File file;
                        memset(&file,0,sizeof(file));
                        if (read(tracker_fd, &file, sizeof(file)) < 0){
                            printError("error while reading tracker response");
                            return -1;
                        }
                        files.push_back(file);
                        cout<<"File "<<i+1<<": "<<file.name<<"\n\tExtension: "<<file.extension<<"\n\tSize: ("<<file.size<<" bytes)\n";
                    }
                    // Get files on the network
                    break;
                case 3:
                    // Exit
                    handle_sig(SIGQUIT);
                    break;
                default:
                    cout<<"Invalid action";
                    break;
            }
        }
    }
    else if (pid == 0){
        // Child - server code
        close(tracker_fd);
        while (true)
        {
            sockaddr_in peer_addr;
            socklen_t peer_addr_len = sizeof(peer_addr);
            int peer_fd = acceptConnection(sock_fd, peer_addr, peer_addr_len);
            if (peer_fd < 0)
            {
                printError("error while accepting connection from peer");
                return -1;
            }
            pid_t pid2 = fork();
            if(pid < 0){
                printError("fork error");
                return -1;
            }
            else if(pid2 == 0){
                // Child - server code
                // Inchid descriptorul vechi si procesez copilul
                close(sock_fd);
                Requests request;
                if (read(peer_fd, &request, sizeof(request)) < 0)
                {
                    printError("error while reading request from peer");
                    return -1;
                }
                int files_count = 0;
                vector<File> files;
                files.clear();
                switch (request){
                    case T_GETFILES:
                        // Trimitem nr + proprietatile fisierelor
                        files_count = getFiles(options.files_path,files);
                        //printf("\nSending files to peer\n");
                        if(write(peer_fd,&files_count,sizeof(files_count)) < 0){
                            printError("error while writing to peer file count");
                            return -1;
                        }
                        for(int i = 0; i < files_count; i++){
                            if(write(peer_fd,&files[i],sizeof(files[i])) < 0){
                                printError("error while writing to peer file");
                                return -1;
                            }
                        }
                        break;
                    default:
                        break;
                }
                close(peer_fd);
                return 0;
            }
            else{
                // Parent - server code
                // Inchid descriptorul nou si reiau loop-ul
                close(peer_fd);
            }
            
        }
    }
    else{
        // Fork error
        printError("fork error");
    }
    return 0;
}