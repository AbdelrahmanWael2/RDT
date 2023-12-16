#include <iostream>
#include <time.h>
#include <string>
#include <cstdint>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <cinttypes>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <vector>
#include <fstream>
#include <string.h>

using namespace std;

#define MAX_BUFFER_SIZE 1024

typedef struct packet
{
    uint16_t cksum;
    uint16_t len;
    uint32_t seq;
    // Data
    char data[MAX_BUFFER_SIZE];
} p;

typedef struct ack_packet
{
    uint16_t cksum;
    uint16_t len;
    uint32_t ackno;
} a;

int sock_fd = 0;
struct sockaddr_in serv_addr;
// ip, port, filename
vector<string> clientData(3);
p *pck;

socklen_t fromlen;
long long byte_count = 0;

void readClientData();

uint16_t calculateChecksum(const char *data, uint16_t len)
{
    uint32_t sum = 0;
    for (uint16_t i = 0; i < len; ++i)
    {
        sum += data[i];
    }
    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return static_cast<uint16_t>(~sum);
}

void initializeClient()
{
    printf("* Client starting ... \n");
    pck = (packet *)malloc(sizeof(packet));

    // Creating socket file descriptor
    if ((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
    { // type of socket is Datagram
        printf("\n Socket creation error \n");
        exit(EXIT_FAILURE);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    readClientData();

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(stoi(clientData[1])); // 8080

    // setting the address given (127.0.0.1)
    if (inet_pton(AF_INET, clientData[0].c_str(), &serv_addr.sin_addr) == -1)
    {
        printf("\n Invalid address\n");
        exit(EXIT_FAILURE);
    }

    strcpy(pck->data, clientData[2].c_str());
    pck->len = clientData[2].size();

    // check sum
    pck->cksum = calculateChecksum(pck->data, pck->len);

    if ((sendto(sock_fd, pck->data, pck->len, 0,
                (struct sockaddr *)&serv_addr, sizeof(serv_addr))) == -1)
    {
        printf("\n Failed to send packet data from Client \n");
        exit(EXIT_FAILURE);
    }
}

void readClientData()
{
    ifstream infile("client.in");
    string line;
    int i = 0;
    while (getline(infile, line))
        clientData[i++] = line;
}

void receiveServerData()
{

    // open output file for server data
    ofstream wf("server.out", ios::out | ios::binary);
    if (!wf)
    {
        cout << "Cannot open file!" << endl;
        return;
    }
    uint32_t last_received = 0; // sequence number of the last recieved packet
    while (true)
    {
        fromlen = sizeof serv_addr;
        packet packet;
        ack_packet ack;
        // receive data in buffer
        byte_count = recvfrom(sock_fd, &packet, sizeof(packet), 0, (struct sockaddr *)&serv_addr, &fromlen);
        if (packet.len < 16)
        {
            wf.write(packet.data, packet.len);
            wf.flush();
            printf("File received successfully \n");
            break;
        }
        printf("Received packet with SEQ %d and LEN %d\n", packet.seq, packet.len);
        if (packet.seq <= last_received)
        {
            // duplicate packet
            ack.ackno = last_received + 1;
        }
        else
        {
            // write received data
            wf.write(packet.data, packet.len);
            wf.flush();
            ack.ackno = packet.seq + 1;
            last_received = packet.seq;
        }
        // Send acknowledgement after receiving and consuming a data packet
        ack.len = 0;

        sendto(sock_fd, &ack, sizeof(ack), 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    }
    wf.close();
    if (!wf.good())
    {
        cout << "Error occurred at writing time!" << endl;
        return;
    }
}

int main()
{
    initializeClient();
    receiveServerData();
    free(pck);
    close(sock_fd);
    return 0;
}