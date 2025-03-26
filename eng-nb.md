ENGINEERING NOTEBOOK

Replication:
1) Deciding the approach to replication: Some options available include a leaderless (state machine/active replication) approach, primary-backup approach, and even a multi-leader approach (scalability in mind for extra credit). Since a chat application is real-time and is expected to produce immediate message delivery, I was inclined to consider the leaderless approach. However, what is particularly concerning is the complexity that will be required to validate each replica and resolve conflicts between the states of each replica. For that reason, I opted for the primary-backup approach, specifically using synchronous updates and acknowledgements. Although this produces a bottleneck at the primary server and requires election/replacement, it is conceptually more straightforward and will ensure greater consistency among replicas, which we have decided is more important than speed. After all, the receiver doesn't know what messages they haven't gotten until they show up, but if messages are mixed up or out of order, it will be evident.

2) Discovery process: We will build from the beginning with the assumption that we can take on additional servers as replicas. As such, before conducting the election, we will engage in a discovery process to ensure communication between the original three servers. 

Upon start-up, each server will send a discovery ping, defined in an intra-server messaging service to each potential peer from a static peer list. It will attempt to contact each possible peer three times. Each peer that sends a discovery response will be added to a peer list for that server. We are opting for the static peer list to reduce configuration overhead while still giving the option to connect from and to a considerable number of replicas. We decided on the intra-server messaging service because it will also help facilitate future elections and leader updates. 


Need to aad update message for login delivery address
