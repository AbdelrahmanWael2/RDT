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
#include <map>
#include <thread>
#include <chrono>
#include <algorithm>

using namespace std;

#define MAX_BUFFER_SIZE 1024
const int INITIAL_CWND = 20;
bool stopSending = true;

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
map<int, packet> packetBuffer;
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

void receiveServerData_Stop_and_Wait()
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
        ack.ackno = last_received + 1;

        // receive data in buffer
        byte_count = recvfrom(sock_fd, &packet, sizeof(packet), 0, (struct sockaddr *)&serv_addr, &fromlen);

        if (packet.len == 0)
        {
            // end receiving
            cout << "File transfer complete" << endl;
            return;
        }
        if (packet.seq == ack.ackno)
        {
            // In-order packet received
            printf("Received packet with SEQ %d and LEN %d\n", packet.seq, packet.len);
            if (calculateChecksum(packet.data, packet.len) == packet.cksum)
            {
                wf.write(packet.data, packet.len);
                wf.flush();
                last_received = packet.seq;
            }
            else
            {
                cout << "wrong checksum" << endl;
                continue;
            }

            // Send acknowledgment after receiving and consuming a data packet
            ack.len = 0;
            ack.ackno++;
            cout << "Sending ACK: " << ack.ackno << endl;
            sendto(sock_fd, &ack, sizeof(ack), 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
        }
        else if (packet.seq > ack.ackno)
        {
            // Out-of-order packet, consider buffering or just continue
            continue;
        }
        // else: Duplicate packet, ignore
    }
    wf.close();
    if (!wf.good())
    {
        cout << "Error occurred at writing time!" << endl;
        return;
    }
}

void sendAcknowledgment(int sockfd, sockaddr_in serv_addr, int ackno, bool stopSending)
{
    cout << "out" << endl;
    while(!stopSending){
    cout << "in" << endl;    
    ack_packet ack;
    ack.ackno = ackno;
    
    ack.len = 0;
    sendto(sockfd, &ack, sizeof(ack), 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
    cout << "Sending ack: " << ack.ackno << endl;
    }
    
}

void writeBufferedPackets(ofstream &wf, int &expectedSeq)
{
    // Write and remove buffered packets that are in sequence
    while (!packetBuffer.empty() && packetBuffer.begin()->first == expectedSeq)
    {
        wf.write(packetBuffer.begin()->second.data, packetBuffer.begin()->second.len);
        wf.flush();
        cout << "File data written for seqno: " << packetBuffer.begin()->second.seq << endl;
        packetBuffer.erase(packetBuffer.begin());
        ++expectedSeq;

        // Send acknowledgment for the highest in-order packet
        sendAcknowledgment(sock_fd, serv_addr, expectedSeq, stopSending);
    }
}


void receiveServerData_Selective_Repeat()
{
    ofstream wf("server.out", ios::out | ios::binary);
    if (!wf)
    {
        cout << "Cannot open file!" << endl;
        return;
    }

    vector<packet> receivedPackets;
    int expectedSeq = 1; // Next expected sequence number
    bool stopSending = true;

    while (true)
    {
        fromlen = sizeof serv_addr;
        packet receivedPacket;
        ack_packet ack;
        byte_count = 0;

        thread acknowledgmentThread(sendAcknowledgment, sock_fd, serv_addr, ref(expectedSeq), ref(stopSending));

         while (true)
        {
            cout << "here" << endl;
            byte_count = recvfrom(sock_fd, &receivedPacket, sizeof(receivedPacket), 0,
                                  (struct sockaddr *)&serv_addr, &fromlen);

            if (byte_count == 0)
            {
                // No data received, continue waiting
                sendAcknowledgment(sock_fd, serv_addr, expectedSeq, stopSending);
            }
            else if (byte_count < 0)
            {
                stopSending = true;
                break;
            }
            else
            {
                // If you received something, stop the acknowledgment thread
                stopSending = true;
                break;
            }
        }

        if (receivedPacket.len == 0)
        {
            // end receiving
            cout << "File transfer complete" << endl;
            return;
        }

        cout << "received : " << receivedPacket.seq << endl;
        // Check if the received packet is within the expected range
        if (receivedPacket.seq == expectedSeq)
        {
            wf.write(receivedPacket.data, receivedPacket.len);
            wf.flush();
            cout << "File data written for seqno: " << receivedPacket.seq << endl;
            ++expectedSeq;

            // Send acknowledgment for the highest in-order packet
            sendAcknowledgment(sock_fd, serv_addr, expectedSeq, stopSending);
            // Check and write any buffered packets that are now in sequence
            writeBufferedPackets(wf, expectedSeq);
        }
        else
        {
            // Duplicate or out-of-order packet, acknowledge the last in-order packet
            cout << "expected seq: " << expectedSeq << endl;
            sendAcknowledgment(sock_fd, serv_addr, expectedSeq, stopSending);

            // Store the out-of-order packet in the buffer
            packetBuffer[receivedPacket.seq] = receivedPacket;
        }

        // Check and write any buffered packets that are now in sequence
        writeBufferedPackets(wf, expectedSeq);
        stopSending = false;
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
    receiveServerData_Selective_Repeat();
    free(pck);
    close(sock_fd);
    return 0;
}