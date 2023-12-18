#include <iostream>
#include <string>
#include <cstring>
#include <thread>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <vector>
#include <numeric>
#include <fstream>
#include <algorithm>
#define MSS 16 // Maximum Segment Size

using namespace std;

const int TIMEOUT_SECONDS = 5;
// const int PORT = 8080;
const int BUFFER_SIZE = 16;
const int INITIAL_CWND = 1;
int PORT;
int SEED;
float PROBABILITY_LOSS;

typedef struct packet
{
    /* Header */
    uint16_t chsum;
    uint16_t len;
    uint32_t seqno;
    /* Data */
    char data[MSS];
} packet;

typedef struct ack_packet
{
    uint16_t chsum;
    uint16_t len;
    uint32_t ackno;
} ack_packet;

typedef struct MessageArgs
{
    sockaddr_in client_address{};
    string filePath;
} MessageArgs;

uint16_t calculateChecksum(const char *data, uint16_t len)
{
    uint32_t sum = std::accumulate(data, data + len, 0);
    while (sum >> 16)
    {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    return static_cast<uint16_t>(~sum);
}

packet make_packet(uint32_t seqno, uint16_t len, char data[])
{
    packet p;
    p.chsum = 0;
    p.len = len;
    p.seqno = seqno;

    p.chsum = calculateChecksum(data, len);

    for (int i = 0; i < len; i++)
        p.data[i] = data[i];
    return p;
}

// read the contents of a file into a vector of packet structs
vector<packet> readFile(char *fileName)
{
    FILE *fp;
    vector<packet> packets;
    char *content = (char *)malloc(10000);
    fp = fopen(fileName, "rb");
    if (fp == nullptr)
        return packets;
    int nBytes = 0;
    int seqno = 1; // Initialize sequence number
    int count = 0;
    while (fread(&content[nBytes], sizeof(char), 1, fp) == 1)
    {
        nBytes++;
        if (nBytes == MSS)
        {
            // cout << "chunk " << content << endl; // correct
            count++;
            packet p = make_packet(seqno++, nBytes, content);
            packets.push_back(p);
            // cout << "packet " << p.data << endl;
            nBytes = 0;
        }
    }
    if (nBytes != 0)
    {
        // count++;
        char last_content[nBytes];
        memcpy(last_content, content, nBytes);
        packet p = make_packet(seqno, nBytes, last_content);
        packets.push_back(p);
        // cout << "last packet " << last_content << endl;
        cout << "size = " << count * 16 << endl;
    }
    fclose(fp);
    free(content);
    return packets;
}

// wait for a specified amount of time for a socket to become readable
int timeOut(int sockfd, ack_packet &ack, sockaddr_in &client_address)
{
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(sockfd, &read_fds);

    // Set up the timeout
    struct timeval timeout;
    timeout.tv_sec = TIMEOUT_SECONDS;
    timeout.tv_usec = 0;

    // Wait for the socket to become readable or for the timeout to expire
    int status = select(sockfd + 1, &read_fds, nullptr, nullptr, &timeout);

    if (status > 0 && FD_ISSET(sockfd, &read_fds))
    {
        // ACK received
        socklen_t client_addr_len = sizeof(client_address);
        long bytes_received = recvfrom(sockfd, &ack, sizeof(ack), 0,
                                       (sockaddr *)&client_address, &client_addr_len);
        cout << "recieved ack :" << ack.ackno << endl;
        // cout << bytes_received << endl;
        if (bytes_received > 0)
        {
            return 1; // Valid ACK received
        }
    }

    return status; // Timeout or error
}

void sendDataChunks_Stop_and_Wait(int sockfd, sockaddr_in client_address, char *fileName)
{
    vector<packet> packets = readFile(fileName);
    unsigned int n = packets.size();
    // for (int i = 0; i < n; i++)
    //     cout << packets[i].seqno << endl; // wrong !!!!!!!!!!!!!!!!!!

    // Congestion Control Variables
    int cwnd = INITIAL_CWND;
    int ssthresh = 64; // ssthresh
    int dupACKcount = 0;
    bool loss = false;

    for (int i = 0; i < static_cast<int>(n);)
    {
        if ((rand() % 100) < (PROBABILITY_LOSS * 100))
        {
            cout << "Simulating packet loss for packet with seqno: " << packets[i].seqno << endl;
            loss = true;
            continue; // Skip sending this packet
        }

        cout << "sending with seqno :" << i + 1 << endl;
        if (!loss)
        {
            sendto(sockfd, &packets[i], sizeof(packets[i]), 0,
                   (sockaddr *)&client_address, sizeof(client_address));
        }
        loss = false;

        // wait acknowledgement from client
        ack_packet ack;
        int expected_ack = i + 2;
        socklen_t client_addr_len = sizeof(client_address);
        int status = timeOut(sockfd, ack, client_address);
        cout << "Status: " << status << endl;

        if (status == -1)
        {
            // An error occurred
            cerr << "Error waiting for socket: " << strerror(errno) << endl;
            return;
        }
        else if (status == 0)
        {
            // The timeout expired
            cerr << "Timeout expired" << endl;

            // Implement timeout actions: Set ssthresh, reduce cwnd, and retransmit
            ssthresh = cwnd / 2;
            cwnd = INITIAL_CWND;
            cout << "Here " << i << endl;
            // Retransmit the current packet
            sendto(sockfd, &packets[i], sizeof(packets[i]), 0,
                   (sockaddr *)&client_address, sizeof(client_address));
            continue;
        }
        else
        {
            if (ack.ackno == expected_ack)
            {
                // Acknowledgment is as expected, move to the next packet
                i++;
                // Reset duplicate ACK count
                dupACKcount = 0;
            }
            else
            {
                // Duplicate ACK received, trigger retransmission
                dupACKcount++;
                if (dupACKcount == 3)
                {
                    // Fast Recovery
                    ssthresh = cwnd / 2;
                    cwnd = ssthresh + 3 * MSS;
                }
                else if (dupACKcount > 3)
                {
                    // Avoid excessive increase during Fast Recovery
                    cwnd += MSS;
                }
                else
                {
                    // Slow Start or Congestion Avoidance
                    cwnd += MSS;
                }

                // Retransmit the current packet
                sendto(sockfd, &packets[i], sizeof(packets[i]), 0,
                       (sockaddr *)&client_address, sizeof(client_address));
            }
        }
    }
}

void sendDataChunks_Selective_Repeat(int sockfd, sockaddr_in client_address, char *fileName)
{
    vector<packet> packets = readFile(fileName);
    unsigned int n = packets.size();

    int base = 0;    // base of the sending window
    int nextSeq = 0; // next sequence number to be sent
    int windowSize = 4;

    vector<bool> ackReceived(n, false);

    while (base < n)
    {
        // Send packets in the window
        for (int i = base; i < min(base + windowSize, static_cast<int>(n)); ++i)
        {
            if (!ackReceived[i])
            {
                cout << "Sending packet with seqno: " << packets[i].seqno << endl;
                sendto(sockfd, &packets[i], sizeof(packets[i]), 0,
                       (sockaddr *)&client_address, sizeof(client_address));
            }
        }

        // Wait for acknowledgments
        int old_base = base;
        for (int i = base; i < min(old_base + windowSize, static_cast<int>(n)); ++i)
        { // cout << "alo"  << endl;
            if (!ackReceived[i])
            {
                ack_packet ack;
                socklen_t client_addr_len = sizeof(client_address);
                int status = timeOut(sockfd, ack, client_address);
                cout << "Status: " << status << endl;

                if (status == -1)
                {
                    cerr << "Error waiting for socket: " << strerror(errno) << endl;
                    return;
                }
                else if (status == 0)
                {
                    // Timeout, retransmit the unacknowledged packets in the window
                    cout << "Timeout expired, retransmitting packets in the window" << endl;

                    // Retransmit packets in the window
                    for (int i = base; i < min(base + windowSize, static_cast<int>(n)); ++i)
                    {
                        if (!ackReceived[i])
                        {
                            cout << "Retransmitting packet with seqno: " << packets[i].seqno << endl;
                            sendto(sockfd, &packets[i], sizeof(packets[i]), 0,
                                   (sockaddr *)&client_address, sizeof(client_address));
                        }
                    }
                    // Continue waiting for acknowledgments
                    continue;
                }
                else
                { // cout << "HERE" << endl;
                    // Acknowledgment received
                    if (ack.ackno == packets[i].seqno + 1)
                    {
                        cout << "Received ACK for packet with seqno: " << ack.ackno << endl;
                        ackReceived[i] = true;

                        // Move the base forward
                        while (base < n && ackReceived[base])
                            ++base;

                        cout << "base: " << base << endl;

                        // Update nextSeq based on the received acknowledgment and window size
                        nextSeq = max(nextSeq, static_cast<int>(ack.ackno) + 1);
                        // break;
                    }
                    else
                    {
                        // Duplicate ACK received, ignore
                    }
                }
            }
        }
    }
}

void handle_connection(void *args)
{
    // Get the socket and client address from the arguments
    int newSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (newSocket < 0)
    {
        cerr << "Error creating socket" << endl;
        return;
    }

    auto *message_args = (MessageArgs *)args;
    sockaddr_in client_address = message_args->client_address;
    string filePath = message_args->filePath;
    // should handle the send of data in chunks
    sendDataChunks_Stop_and_Wait(newSocket, client_address, (char *)filePath.c_str());
    // Close the connection
    close(newSocket);
}

void waitForConnection(int sockfd)
{
    while (true)
    {
        // Wait for a connection
        char buffer[BUFFER_SIZE];
        cout << "Server waiting for a connection..." << endl;
        sockaddr_in client_addr{};
        socklen_t client_addr_len = sizeof(client_addr);
        long bytes_received = recvfrom(sockfd, buffer, BUFFER_SIZE, 0,
                                       (sockaddr *)&client_addr, &client_addr_len);
        if (bytes_received < 0)
        {
            cerr << "Failed to accept connection" << endl;
            continue;
        }

        // Create a new thread to handle the connection
        MessageArgs messageArgs;
        messageArgs.client_address = client_addr;
        string str;
        str = buffer;
        messageArgs.filePath = str.substr(0, bytes_received);
        pthread_t thread;
        pthread_create(&thread, nullptr, reinterpret_cast<void *(*)(void *)>(handle_connection), &messageArgs);
    }
}

bool readServerConfig(const string &filename)
{
    ifstream inputFile(filename);
    if (!inputFile.is_open())
    {
        cerr << "Error opening file: " << filename << endl;
        return false;
    }

    // Read parameters from the file
    if (!(inputFile >> PORT >> SEED >> PROBABILITY_LOSS))
    {
        cerr << "Error reading parameters from file" << endl;
        return false;
    }

    // Seed the random number generator
    srand(SEED);

    inputFile.close();
    return true;
}

int main()
{
    if (!readServerConfig("server.in"))
    {
        return 1;
    }

    // Create a socket
    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
    {
        cerr << "Error creating socket" << endl;
        return 1;
    }

    // bind the socket to the port
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    if (bind(sockfd, (sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        cerr << "Error binding socket to port" << endl;
        return 1;
    }

    // waiting for connections
    waitForConnection(sockfd);

    return 0;
}
