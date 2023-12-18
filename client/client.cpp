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
#include <algorithm>

using namespace std;

#define MAX_BUFFER_SIZE 1024
const int INITIAL_CWND = 20;

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

    while (true)
    {
        fromlen = sizeof serv_addr;
        packet receivedPacket;
        ack_packet ack;

        // Receive the packet
        byte_count = recvfrom(sock_fd, &receivedPacket, sizeof(receivedPacket), 0,
                              (struct sockaddr *)&serv_addr, &fromlen);

        if (receivedPacket.len == 0)
        {
            // end receiving
            cout << "File transfer complete" << endl;
            return;
        }

        // Check if the received packet is within the expected window
        if (receivedPacket.seq >= expectedSeq && receivedPacket.seq < expectedSeq + INITIAL_CWND)
        {
            // Check for duplicate packets
            auto it = find_if(receivedPackets.begin(), receivedPackets.end(),
                              [&](const packet &p)
                              { return p.seq == receivedPacket.seq; });

            if (it == receivedPackets.end())
            {
                cout << "Received packet with seqno: " << receivedPacket.seq << endl;
                receivedPackets.push_back(receivedPacket);

                // Sort received packets based on sequence number
                sort(receivedPackets.begin(), receivedPackets.end(),
                     [](const packet &a, const packet &b)
                     { return a.seq < b.seq; });

                // Update expected sequence number
                while (!receivedPackets.empty() && receivedPackets.front().seq == expectedSeq)
                {
                    wf.write(receivedPackets.front().data, receivedPackets.front().len);
                    wf.flush();
                    cout << "File data written for seqno: " << receivedPackets.front().seq << endl;
                    receivedPackets.erase(receivedPackets.begin());
                    ++expectedSeq;
                }

                // Send acknowledgment for the highest in-order packet
                ack.len = 0;
                ack.ackno = expectedSeq;
                sendto(sock_fd, &ack, sizeof(ack), 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
            }
            else
            {
                // Duplicate packet, ignore
                cout << "Duplicate packet with seqno: " << receivedPacket.seq << endl;

                // Send acknowledgment for the duplicate packet
                ack.len = 0;
                ack.ackno = expectedSeq;
                sendto(sock_fd, &ack, sizeof(ack), 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
            }
        }
        else
        {
            // Packet is outside the window range, ignore
            //cout << "Packet out of range with seqno: " << receivedPacket.seq << endl;

            // Send acknowledgment for the highest in-order packet
            ack.len = 0;
            ack.ackno = expectedSeq;
            cout << "Sending ack : " << ack.ackno << endl;
            sendto(sock_fd, &ack, sizeof(ack), 0, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
        }
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