#!/bin/bash
g++ p2p_client.cpp -o client
g++ p2p_client.cpp -o ./client1/client 2>/dev/null
g++ p2p_client.cpp -o ./client2/client 2>/dev/null
g++ p2p_client.cpp -o ./client3/client 2>/dev/null
g++ p2p_client.cpp -o ./client4/client 2>/dev/null
g++ tracker.cpp -o tracker
