# NDFEX

Notre Dame Fake Exchange

    This project is intended for the course on High-Frequency Trading Technologies at the University of Notre Dame. It emulates a real exchange
    for students to practice trading strategies.

    Design
        - Matching Engine: Exists per-symbol to add orders to the order book, emit changes to the subscriber, match orders and emit trades.
            - Market Data feed: Takes order book changes from the subscriber and sends them out via multicast.
            - Order Entry Gateway: A TCP server that allows clients to enter orders using the order entry protocol.
            - Recovery snapshot feed: Resends the current order book periodically

        - Security Definition Feed
            - sends out security definitions on multicast periodically

        - PCAP Recorder
            - Records all traffic to / from exchange server for student debugging

        Trading bots
            - Microprice market makers - try to balance buys and sells
            - VWAP Broker - gets a random quantity to execute every minute and sends it semi-randomly over that time period
            - Small retail traders - randomly send 1 lots to cross the bid / ask




