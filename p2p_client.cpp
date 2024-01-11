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
        char header[1024];
        genHeaderClient(header,"P2P Client");
        printf("%s",header);
        while(true){
            close(tracker_fd);
            tracker_fd = connectToTracker(tracker_addr);
            if (tracker_fd < 0){
                return -1;
            }
            cout<<"\n\033[4mEnter number of action you wish to execute:\033[0m \n\t (1) Get current peers\n\t (2) Get files on the network\n\t (3) Exit\n\nCommand: ";
            char option[2];
            scanf("%s",option);
            int action = atoi(option);
            Requests ping;
            system("clear");
            sockaddr_in other_peer_addr;
            int peers_count;
            int files_count;
            int file_size;
            int other_peer;
            int peer_index;
            char file_path[512];
            char file_number[3];
            int file_index;
            int error_handle;
            FILE* file_fd;
            vector<File> files;
            files.clear();
            vector<sockaddr_in> peers_with_file;
            peers_with_file.clear();
            SearchOpt search;
            memset(&search,0,sizeof(search));
            char size_str[10];
            switch(action){
                case 1:
                    // Get current peers
                    ping = T_GETPEERS;
                    memset(header,0,sizeof(header));
                    genHeaderClient(header,"PEERS");
                    printf("%s",header);
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
                    memset(header,0,sizeof(header));
                    genHeaderClient(header,"FILES");
                    printf("%s",header);
                    cout<<"Getting files...\n";
                    if (send(tracker_fd, &ping, sizeof(ping), 0) < 0){
                        printError("error while sending request to tracker");
                        return -1;
                    }
                    // TODO: send criteria to search by
                    cout<<"Search file by name (type - to search all files):";
                    scanf("%s",search.name);
                    if(strcmp(search.name,"-") != 0){
                        search.by_name = 1;
                    }else{
                        search.by_name = 0;
                    }
                    cout<<"Search file by extension (type - to search all extensions):";
                    scanf("%s",search.extension);
                    if(strcmp(search.extension,"-") != 0){
                        search.by_extension = 1;
                    }else{
                        search.by_extension = 0;
                    }
                    cout<<"Minimum size (type 0 for any; example 5K | 15M):";
                    scanf("%s",size_str);
                    search.min_size = convertFileSize(size_str);
                    cout<<"Maximum size (type 0 for any; example 5K | 15M):";
                    scanf("%s",size_str);
                    search.max_size = convertFileSize(size_str);
                    if (send(tracker_fd, &search, sizeof(search), 0) < 0){
                        printError("error while sending request to tracker");
                        return -1;
                    }
                    if (read(tracker_fd, &files_count, sizeof(files_count)) < 0){
                        printError("error while reading tracker response");
                        return -1;
                    }
                    //cout<<"There are "<<files_count<<" files on the network\n";
                    for(int i = 0; i < files_count; i++){
                        File file;
                        memset(&file,0,sizeof(file));
                        if (read(tracker_fd, &file, sizeof(file)) < 0){
                            printError("error while reading tracker response");
                            return -1;
                        }
                        if(searchFile(options.files_path,file.hash)){
                            continue;
                        }
                        else{
                            files.push_back(file);
                        }
                    }
                    // Get files on the network
                    files_count = files.size();
                    if(files_count == 0){
                        cout<<"No new files on the network\n";
                        break;
                    }
                    for(int i = 0; i < files_count; i++){
                        cout<<"("<<i+1<<"): "<<files[i].name<<" - "<<files[i].size<<" bytes\n";
                    }
                    cout<<"\033[4mEnter number of file you wish to download (0 for none):\033[0m ";
                    scanf("%s",file_number);
                    file_index = atoi(file_number);
                    if(file_index < 1 || file_index > files_count){
                        cout<<"Invalid file number\n";
                        break;
                    }
                    tracker_fd = connectToTracker(tracker_addr);
                    ping = T_GETFILE;
                    if (send(tracker_fd, &ping, sizeof(ping), 0) < 0){
                        printError("error while sending request to tracker");
                        return -1;
                    }
                    if (send(tracker_fd, &files[file_index-1].hash, sizeof(files[file_index-1].hash), 0) < 0){
                        printError("error while sending request to tracker");
                        return -1;
                    }
                    if (read(tracker_fd, &peers_count, sizeof(peers_count)) < 0){
                        printError("error while reading tracker response");
                        return -1;
                    }
                    if(peers_count == 0){
                        cout<<"No peers with file currently online\n";
                        break;
                    }
                    for(int i = 0; i < peers_count; i++){
                        if (read(tracker_fd, &other_peer_addr, sizeof(other_peer_addr)) < 0){
                            printError("error while reading tracker response");
                            return -1;
                        }
                        peers_with_file.push_back(other_peer_addr);
                    }
                    file_size = files[file_index-1].size;
                    sprintf(file_path,"%s/%s",options.files_path,files[file_index-1].name);
                    file_fd = fopen(file_path,"wb");
                    error_handle = 0;
                    for(int i = 0; i < file_size/CHUNK_SIZE; i++){
                        other_peer = socket(AF_INET,SOCK_STREAM,0);
                        peer_index = i % peers_with_file.size();
                        if(connect(other_peer,(struct sockaddr*)&peers_with_file[peer_index],sizeof(peers_with_file[0])) < 0){
                            printError("couldn't connect to peer to request file");
                            peers_with_file.erase(peers_with_file.begin()+peer_index);
                            i--;
                            if(peers_with_file.size() == 0){
                                cout<<"No peers with file currently online\n";
                                error_handle = 1;
                                break;
                            }
                            continue;
                        }
                        ping = P_GETFILE;
                        // Trimitem request-ul
                        getFileChunk(other_peer,files[file_index-1].name,i*CHUNK_SIZE,CHUNK_SIZE,file_fd);
                    }
                    if(error_handle == 1){
                        cout<<"Couldn't download file\n";
                        fclose(file_fd);
                        // Delete file
                        remove(file_path);
                        break;
                    }
                    other_peer = socket(AF_INET,SOCK_STREAM,0);
                    for(int i = 0; i < peers_with_file.size(); i++){
                        if(connect(other_peer,(struct sockaddr*)&peers_with_file[i],sizeof(peers_with_file[0])) < 0){
                            printError("couldn't connect to peer to request file");
                            peers_with_file.erase(peers_with_file.begin()+i);
                            i--;
                            if(peers_with_file.size() == 0){
                                cout<<"No peers with file currently online\n";
                                error_handle = 1;
                                break;
                            }
                            continue;
                        }
                        ping = P_GETFILE;
                        // Trimitem request-ul
                        getFileChunk(other_peer,files[file_index-1].name,(file_size/CHUNK_SIZE)*CHUNK_SIZE,file_size%CHUNK_SIZE,file_fd);
                        break;
                    }
                    ftruncate(fileno(file_fd),file_size);
                    fclose(file_fd);
                    if(error_handle == 1){
                        cout<<"Couldn't download file\n";
                        // Delete file
                        remove(file_path);
                        break;
                    }
                    else{
                        cout<<"File downloaded successfully\n";
                    }
                    break;
                case 3:
                    // Exit
                    memset(header,0,sizeof(header));
                    genHeaderClient(header,"EXIT");
                    printf("%s",header);
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
                char hash[65];
                char name[256];
                int offset;
                int chunk_size;
                FILE* file_fd;
                char file_path[512];
                int have_file;
                char chunk[CHUNK_SIZE];
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
                    case T_GETFILE:
                        // Trimitem fisierul
                        read(peer_fd,hash,sizeof(hash));
                        have_file = searchFile(options.files_path,hash);
                        write(peer_fd,&have_file,sizeof(have_file));
                        break;
                    case P_GETFILE:
                        read(peer_fd,name,256);
                        read(peer_fd,&offset,sizeof(offset));
                        read(peer_fd,&chunk_size,sizeof(chunk_size));
                        sprintf(file_path,"%s/%s",options.files_path,name);
                        file_fd = fopen(file_path,"rb");
                        fseek(file_fd,offset,SEEK_SET);
                        memset(chunk,0,sizeof(chunk));
                        fread(chunk,sizeof(char),chunk_size,file_fd);
                        write(peer_fd,chunk,chunk_size);
                        fclose(file_fd);
                    default:
                        break;
                }
                close(peer_fd);
                exit(0);
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