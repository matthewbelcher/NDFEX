#!/usr/bin/env python3
import os
import sys
import argparse
from news_server import NewsServer

def main():
    parser = argparse.ArgumentParser(description='Run the financial news headline generator server')
    
    parser.add_argument('--api-key', type=str, help='OpenAI API key (or set OPENAI_API_KEY env var)')
    parser.add_argument('--symbols', type=str, nargs='+', default=['GOLD', 'BLUE'], 
                        help='Symbols to generate news for')
    parser.add_argument('--interval', type=int, default=60,
                        help='Interval between headlines in seconds')
    parser.add_argument('--output-dir', type=str, default='./headlines',
                        help='Directory to store headlines')
    
    args = parser.parse_args()
    
    # Get API key from args or environment
    api_key = args.api_key or os.environ.get('OPENAI_API_KEY')
    if not api_key:
        print("ERROR: OpenAI API key must be provided via --api-key or OPENAI_API_KEY environment variable")
        sys.exit(1)
    
    print(f"Starting news server with symbols: {args.symbols}")
    print(f"Headlines will be generated every {args.interval} seconds")
    print(f"Output directory: {args.output_dir}")
    
    # Initialize and start the server
    server = NewsServer(api_key, args.symbols, args.interval, args.output_dir)
    
    try:
        server.start()
        print("News server started. Press Ctrl+C to stop.")
        
        # Keep the main thread running
        while True:
            input("Press Enter to print server status (Ctrl+C to exit)...\n")
            print(f"Server running: {server.running}")
            print(f"Headline count: {len(server.headlines)}")
            print(f"Subscriber count: {len(server.subscribers)}")
            
    except KeyboardInterrupt:
        print("\nShutting down news server...")
        server.stop()
        print("News server stopped.")

if __name__ == "__main__":
    main()
