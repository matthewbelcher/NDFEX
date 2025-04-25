
import requests
base_url = "http://127.0.0.1:8080"
def create_client(username, password):
    url = f"{base_url}/register"
    data = {
        "username": username,
        "password": password
    }
    response = requests.post(url, data=data)
    if response.status_code == 200:
        #print(f"Successfully created client: {username}")
        return True
    else:
        #print(f"Failed to create client: {username} - {response.text}")
        return False
def get_holdings(username, password):
    url = f"{base_url}/holdings"
    params = {
        "username": username,
        "password": password
    }
    response = requests.get(url, params=params)
    if response.status_code == 200:
        #print(f"Holdings for {username}: {response.text}")
        return True
    else:
        #print(f"Failed to retrieve holdings for {username} - {response.text}")
        return False

# Send POST requests to create new clients
assert create_client("alice", "pass123") == False, "alice should exist"
assert create_client("bob", "bobpass") == False, "bob should exist"
assert create_client("charlie", "charliepass") == True, "charlie should be added"

assert get_holdings("alice", "pass123") == True, "alice password should be correct"
assert get_holdings("charlie", "charliepass") == True, "charlie password should be correct"
assert get_holdings("bob", "pas") == False, "bob password should be wrong"
assert get_holdings("bob", "hunter2") == True, "bob password should be correct"