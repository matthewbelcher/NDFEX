# ETF Engine Report

## Overview
This is meant as an addition to the existing NDFEX to facilitate arbitrage opportunities for future classes to build strategies around. The ETF Engine is a system that facilitates the creation, redemption, and management of ETFs (Exchange-Traded Funds) for clients. The system provides a RESTful interface that allows clients to interact with the ETF engine via HTTP requests. The core features include client registration, ETF creation, ETF redemption, and retrieval of client holdings.

## Core Features

### 1. **Client Registration**
   Clients can register with the system by providing a username and password. Upon successful registration, they are added to the system, and their credentials are securely stored.

   - **Endpoint**: `/register` (POST)
   - **Parameters**:
     - `username`: The client's username.
     - `password`: The client's password.
   - **Response**:
     - `200 OK`: Client successfully registered.
     - `400 Bad Request`: Missing or invalid parameters.

### 2. **ETF Creation**
   Clients can create ETFs by specifying the ETF name and the amount to be created. The system checks if the client has sufficient underlying assets to create the requested ETF.

   - **Endpoint**: `/create` (POST)
   - **Parameters**:
     - `username`: The client's username.
     - `password`: The client's password.
     - `etf_name`: The name of the ETF to be created.
     - `amount`: The amount of the ETF to be created.
   - **Response**:
     - `200 OK`: ETF created successfully.
     - `400 Bad Request`: Insufficient funds or invalid parameters.
     - `401 Unauthorized`: Authentication failed.

### 3. **ETF Redemption**
   Clients can redeem an ETF for the underlying assets. The system verifies that the client holds the necessary amount of the ETF and then redeems it.

   - **Endpoint**: `/redeem` (POST)
   - **Parameters**:
     - `username`: The client's username.
     - `password`: The client's password.
     - `amount`: The amount of ETF to redeem.
   - **Response**:
     - `200 OK`: ETF redeemed successfully.
     - `400 Bad Request`: Insufficient ETF holdings or invalid parameters.
     - `401 Unauthorized`: Authentication failed.

### 4. **Get Client Holdings**
   Clients can retrieve a list of their ETF holdings, including the types and amounts of ETFs they own.

   - **Endpoint**: `/holdings` (GET)
   - **Parameters**:
     - `username`: The client's username.
     - `password`: The client's password.
   - **Response**:
     - `200 OK`: A JSON response containing the client's ETF holdings.
     - `401 Unauthorized`: Authentication failed.

## System Architecture

### 1. **ETF Engine**
   The `ETFEngine` class is the core of the system. It manages clients, handles ETF operations, and interacts with the HTTP server. The `ETFEngine` is responsible for:

   - Loading client data from a file.
   - Validating client credentials.
   - Handling ETF creation, redemption, and holdings management.

   **Key Methods**:
   - `load_clients_from_file()`: Loads client data from a file.
   - `start_rest_listener()`: Starts the HTTP server to listen for incoming requests.
   - `shutdown()`: Shuts down the engine and cleans up resources.

### 2. **HTTP Server**
   The `HTTPServer` class handles HTTP requests and interacts with the `ETFEngine` to perform ETF operations. It is designed to handle the following routes:

   - `/register`: Handles client registration.
   - `/create`: Handles ETF creation.
   - `/redeem`: Handles ETF redemption.
   - `/holdings`: Retrieves client ETF holdings.

   **Key Methods**:
   - `handle_request()`: Routes the incoming requests to the appropriate handler.
   - `handle_register_request()`: Handles client registration.
   - `handle_create_etf_request()`: Handles ETF creation.
   - `handle_redeem_etf_request()`: Handles ETF redemption.
   - `handle_holdings_request()`: Retrieves and returns client holdings.

### 3. **Client Management**
   The `AllClients` class stores the details of all clients, including their credentials and holdings. It provides methods for adding new clients, authenticating clients, and managing ETF holdings.


## Challenges
    One issue I had in developing this system was finding a source of truth for the positions of the clients, as etf creation and redemption requires a clearing house to validate holdings of authorized participants.


## Future Areas of Improvement

### 1. **ETF Composition**
   One possible enhancement for the ETF Engine is to allow clients to create ETFs with varying compositions of underlying assets. Each ETF could consist of a specific set of symbols with specified amounts, and clients could create ETFs based on these compositions. The system would need to validate that clients have the necessary underlying assets before allowing ETF creation. 

   **Example**: An ETF called "Tech ETF" could consist of:
   - 2 units of Symbol 3 (e.g., Apple)
   - 1 unit of Symbol 4 (e.g., Google)
   - 3 units of Symbol 5 (e.g., Microsoft)

   This would require changes in the `ETFEngine` to support dynamic ETF creation based on symbol compositions.

   **Benefit**: This improvement would allow for more sophisticated ETF management and could provide more flexibility to clients, enabling them to create ETFs with specific exposure to different asset classes.

### 2. **Real-Time Stock Prices**
   The system could be extended to include real-time stock price data, allowing the ETF engine to dynamically adjust the value of ETFs based on market conditions. This would involve integrating with external APIs or data sources to fetch stock prices and calculate the value of the ETFs accordingly.

### 3. **Audit Trail**
   Implementing an audit trail that logs all client interactions (e.g., ETF creation, redemption, transfers) would improve transparency and allow for better tracking and debugging of the system.
