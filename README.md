S.H.A.M. Protocol (Reliable UDP)
This project involves creating a custom transport-layer protocol named S.H.A.M. built on top of unreliable UDP. It simulates core TCP features like connection management, reliability, and flow control.

🏗️ 1. The S.H.A.M. Packet Structure
Every piece of data sent over the network must be wrapped in a specific header. This allows the receiver to know the order of data and manage errors.

The Header Fields:

Sequence Number (seq_num): Tracks the position of data in the byte stream.

Ack Number (ack_num): Tells the sender which byte the receiver expects next (Cumulative ACK).

Flags: Control bits for connection (SYN to start, ACK to confirm, FIN to end).

Window Size: Used for Flow Control to prevent the sender from overwhelming the receiver.

🤝 2. Connection Management
You must implement the "polite" way computers talk to each other.

Three-Way Handshake (Start)
Client: "I want to connect (SYN)."

Server: "I hear you, I'm ready too (SYN-ACK)."

Client: "Got it, let's start (ACK)."

Four-Way Handshake (End)
Initiator: "I'm done sending (FIN)."

Responder: "Understood (ACK)."

Responder: "I'm also done (FIN)."

Initiator: "Goodbye (ACK)."

🛠️ 3. Reliability & Flow Control
Since UDP is unreliable (it drops packets), your code must handle the "fixing" part.

Sliding Window: The sender can send a "burst" of packets (e.g., 10) before needing an ACK.

Cumulative ACKs: If the receiver gets packets 1, 2, and 4, it only ACKs for "3" (meaning: "I have up to 2, I need 3 next").

Retransmission (RTO): If an ACK doesn't arrive in 500ms, the sender assumes the packet was lost and sends it again.

Flow Control: The receiver tells the sender how much space is left in its buffer. The sender must stop if the buffer is full.

🚀 4. Running the Programs
Server
Bash

./server <port> [--chat] [loss_rate]
Client
File Transfer: ./client <ip> <port> <input_file> <output_name> [loss_rate]

Chat Mode: ./client <ip> <port> --chat [loss_rate]

Note: Use the loss_rate (0.0 to 1.0) to test how well your protocol handles dropped packets.

📝 5. Logging & Verification (Evaluation)
To pass the evaluation, your shell environment must support a verbose logging mode.

Activate Logging: Run export RUDP_LOG=1 before starting your programs.

Log Files: Your program must generate server_log.txt and client_log.txt with microsecond-precision timestamps.

MD5 Checksum: In file mode, the server must print the MD5 hash of the received file to prove the data wasn't corrupted.

Format: MD5: <32-character_hash>

💻 6. Compilation Guide
Linux
Bash

sudo apt install libssl-dev
gcc server.c -o server -lcrypto
MacOS
Bash

brew install openssl
gcc server.c -o server -I$(brew --prefix openssl)/include -L$(brew --prefix openssl)/lib -lcrypto
💡 Implementation Tips
Use select(): In chat mode, use select() to watch both the keyboard (stdin) and the network socket simultaneously.

Byte-based Sequences: Remember that sequence numbers should track the total bytes sent, not just packet counts.

Simulated Loss: Before processing a packet, generate a random number. If it's less than loss_rate, simply ignore/drop the packet to trigger your retransmission logic.
