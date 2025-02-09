# NDFEX

Notre Dame Fake Exchange

    This project is intended for the course on High-Frequency Trading Technologies at the University of Notre Dame. It is a toy version of a real exchange
    for students to practice trading strategies.

    Design
        - Matching Engine: Exists per-symbol to add orders to the order book, emit changes to the subscriber, match orders and emit trades.
            - Market Data : Takes order book changes from the subscriber and sends them out via multicast.
            - Order Entry: A TCP server that allows clients to enter orders using the order entry protocol.            
            - Order Ladder: The actual book management and matching
            - ME Broker: handles queueing between client order entry server and matching engine

        - Bot Runner: Runs a collection of very simple trading strategies to create the illusion of an active market

    Future:
    
        - PCAP Recorder
            - Records all traffic to / from exchange server for student debugging

        More Trading bots
            - Microprice market makers - try to balance buys and sells, stacks
            - VWAP Broker - gets a random quantity to execute every minute and sends it semi-randomly over that time period

