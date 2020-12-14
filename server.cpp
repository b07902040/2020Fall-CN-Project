#include <iostream>
#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <unistd.h> 
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include "opencv2/opencv.hpp"
#include <signal.h>
#include <pthread.h>

#define BUFF_SIZE 1024
#define Video_buff_size 8192
#define buffer_frame 300
using namespace std;
using namespace cv;

int buffer_end[5];
int buffer_img_size[5];
int end_frame[5];
int get_i[5];
int send_i[5];
uchar *big_buffer[50][buffer_frame];

struct args {
    int clientFD, ci;
};

void send_file(int sockfd, int filefd){
    char data[1024];
    int filesize = lseek(filefd, 0, SEEK_END);
    lseek(filefd, 0, SEEK_SET);
    int count = filesize/1024;
    send(sockfd, &count, sizeof(int), 0);
    for ( int i = 0; i < count; i++ ) {
        read(filefd, data, 1024);
        if (send(sockfd, data, 1024, 0) == -1) {
            perror("[-]Error in sending file.");
            exit(1);
        }
        bzero(data, 0);
    }
    int remain = filesize-count*1024;
    if ( remain == 0 ) {
        remain = -1;
        send(sockfd, &remain, sizeof(int), 0);
        return;
    }
    send(sockfd, &remain, sizeof(int), 0);
    read(filefd, data, remain);
    send(sockfd, data, remain, 0);
    close(filefd);
    return;
}

void write_file(int sockfd, char *filename){
    int filefd;
    char buffer[1024];
    filefd = open(filename, O_CREAT | O_WRONLY, S_IRWXU | S_IRWXG | S_IRWXO);
    int count;
    recv(sockfd, &count, sizeof(int), 0);
    for ( int i = 0; i < count; i++ ) {
        recv(sockfd, buffer, 1024, 0);
        write(filefd, buffer, 1024);
        bzero(buffer, 0);
    }
    int remain;
    recv(sockfd, &remain, sizeof(int), 0);
    if ( remain == -1 ) return;
    recv(sockfd, buffer, remain, 0);
    write(filefd, buffer, remain);
    close(filefd);
    return;
}

void *video_buffering(void *input){
    int clientFD = ((struct args*)input)->clientFD;
    int ci = ((struct args*)input)->ci;
    int imgSize;
    int inst, idx, send_n;
    send_i[ci%5] = 0;
    while(1) {
        printf("get: %d send: %d\n", get_i[ci%5], send_i[ci%5]);
        if ( send_i[ci%5] == end_frame[ci%5] ) {
            int end = -1;
            send(clientFD, &end, sizeof(int), 0);
            break;
        }
        send(clientFD, &imgSize, sizeof(int), 0);
        uchar buffer[imgSize+5];
        uchar *iptr;
        iptr = buffer;
        memcpy(buffer, big_buffer[ci][send_i[ci%5]%buffer_frame], imgSize);
        idx = 0;
        imgSize = buffer_img_size[ci%5];
        while ( idx < imgSize ) {
            send_n = send(clientFD, iptr, min(Video_buff_size, imgSize-idx), 0);
            if(send_n < 0 ) {
                perror("ERROR");
            }
            idx += send_n;
            iptr += send_n;
        }
        send_i[ci%5]++;
        recv(clientFD, &inst, sizeof(int), 0);
        if ( inst == 2 ) {
            buffer_end[ci%5] = 1;
            break;
        }
    }
    pthread_exit(0);
}

void *doInChildThread(void *input){
    int clientFD = ((struct args*)input)->clientFD;
    int ci = ((struct args*)input)->ci;
    while(1) {
        char parma[BUFF_SIZE] = {};
        int exist = recv(clientFD, parma, 1024, 0);
        if ( exist == 0 ) {
            break;
        }
        //printf("recvbuf: %s", parma);
        if ( strncmp(parma, "ls", 2) == 0 ) { // ls request
            struct dirent **entry_list;
            int count;
            char filename[100] = "./server_data/";
            count = scandir(filename, &entry_list, 0, alphasort);
            if (count < 0) {
                perror("scandir");
                continue;
            }
            send(clientFD, &count, sizeof(int), 0);
            for (int i = 2; i < count; i++) {
                struct dirent *entry;
                
                entry = entry_list[i];
                //entry->d_name[strcspn(entry->d_name, "\0")] = '\n';
                int len = sizeof(entry->d_name);
                send(clientFD, &len, sizeof(int), 0);
                //printf("%s\n", entry->d_name);
                send(clientFD, entry->d_name, sizeof(entry->d_name), 0);
                free(entry);
            }
            free(entry_list);
        }
        else if ( strncmp(parma, "put", 3) == 0 ) {
            //chdir("./server_data");
            
            char filename[100] = "./server_data/";
            strcat(filename, &parma[4]);
            filename[strcspn(filename, "\n")] = '\0';
            write_file(clientFD, filename);
        }
        else if ( strncmp(parma, "get", 3) == 0 ) {
            char filename[100] = "./server_data/";
            strcat(filename, &parma[4]);
            filename[strcspn(filename, "\n")] = '\0';
            int filefd;
            filefd = open(filename, O_RDONLY);
            if ( filefd <= 1 ) {
                send(clientFD, "ack", sizeof(char)*3, 0);
            }
            else {
                send(clientFD, parma, sizeof(parma), 0);
                send_file(clientFD, filefd);
            }
        }
        else if ( strncmp(&parma[0], "play", 4) == 0 ) {
            char filename[100] = "./server_data/";
            strcat(filename, &parma[5]);
            filename[strcspn(filename, "\n")] = '\0';
            int filefd;
            filefd = open(filename, O_RDONLY);
            int error = -1;
            if ( filefd <= 1 ) {
                send(clientFD, &error, sizeof(int), 0);
                continue;
            }
            Mat imgServer[buffer_frame];
            VideoCapture cap(filename);
            int width = cap.get(CV_CAP_PROP_FRAME_WIDTH);
            int height = cap.get(CV_CAP_PROP_FRAME_HEIGHT);
            buffer_img_size[ci%5] = width*height*3;
            send(clientFD, &width, sizeof(int), 0);
            send(clientFD, &height, sizeof(int), 0);
            for ( int i = 0; i < buffer_frame; i++ ) {
                big_buffer[ci][i] = (uchar*)malloc(buffer_img_size[ci%5]*sizeof(uchar));
                imgServer[i] = Mat::zeros(height,width, CV_8UC3);    
                if(!imgServer[i].isContinuous()){
                    imgServer[i] = imgServer[i].clone();
                }
                big_buffer[ci][i] = imgServer[i].data;
            }
            pthread_t pid;
            struct args *sinput = (struct args *)malloc(sizeof(struct args));
            sinput->clientFD = clientFD;
            sinput->ci = ci;
            buffer_end[ci%5] = 0;
            get_i[ci%5] = 0;
            end_frame[ci%5] = -1;
            pthread_create(&pid, NULL, video_buffering, (void*)sinput);
            while(1){
                if ( buffer_end[ci%5] == 1 ) {
                    break;
                }
                if ( get_i[ci%5]-send_i[ci%5] > 250 ) {
                    printf("To quick\n");
                    sleep(1);
                }
                cap >> imgServer[get_i[ci%5]%buffer_frame];
                if ( imgServer[get_i[ci%5]%buffer_frame].empty() ) {
                    end_frame[ci%5] = get_i[ci%5];
                    break;
                }
                //printf("%d\n", buffer_img_size[ci%5]);
                get_i[ci%5]++;
                //memcpy(big_buffer[ci][get_i[ci%5]%buffer_frame], imgServer.data, buffer_img_size[ci%5]);
            }
            pthread_join(pid, NULL);
            /*for ( int i = 0; i < 300; i++ ) {
                printf("%d\n", i);
                free(big_buffer[ci][i]);
            }*/
            cap.release();
        }
        bzero(parma, BUFF_SIZE);
        printf("Ready for next command\n");
    }
    printf("close Socket\n");
    close(clientFD);
    pthread_exit(0);
}

int main(int argc, char** argv){
    signal(SIGPIPE, SIG_IGN);
    int addrLen = sizeof(struct sockaddr_in);
    char sendbuf[BUFF_SIZE] = {};
    char recvbuf[BUFF_SIZE] = {};
    int serverFD, clientFD, port = atoi(argv[1]);
    
    // Create Socket
    serverFD = socket(AF_INET , SOCK_STREAM , 0);
    if (serverFD == -1){
        printf("socket() call failed!!\n");
        return 0;
    }
    // Prepare socketaddr structure
    struct sockaddr_in server_info, client_info;
    bzero(&server_info, sizeof(server_info));
    server_info.sin_family = AF_INET;         //IPv4
    server_info.sin_addr.s_addr = INADDR_ANY; //IP
    server_info.sin_port = htons(port);       //Port
    // Bind Socket and socketaddr
    if( bind(serverFD,(struct sockaddr *)&server_info , sizeof(server_info)) < 0) {
        printf("Can't bind() socket\n");
        return 0;
    }

    // Create folder for client
    struct stat st = {0};
    if ( stat("./server_data", &st) == -1 ) {
        mkdir("./server_data", 0777);
        //printf("mkdir success\n");
    }
    pthread_t pid[10];
    listen(serverFD , 10);
    int thread_i = 0;
    while(1) { 
        std::cout <<  "Waiting for connections...\n"
                <<  "Server Port:" << port << std::endl;
        // Accept
        clientFD = accept(serverFD, (struct sockaddr *)&client_info, (socklen_t*)&addrLen);   
        if (clientFD < 0) {
            printf("accept failed!");
            return 0;
        }
        struct args *input = (struct args *)malloc(sizeof(struct args));
        input->clientFD = clientFD;
        input->ci = (thread_i)%5;
        pthread_create(&pid[thread_i], NULL, doInChildThread,(void*)input);
        thread_i++;
    }
    return 0;
}