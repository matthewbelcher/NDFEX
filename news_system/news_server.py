import time
import threading
import json
import os
from datetime import datetime
from headline_generator import HeadlineGenerator

class NewsServer:
    def __init__(self, api_key, symbols=None, headline_interval=60, output_dir="./headlines"):
        """
        Args:
            api_key: OpenAI API key
            symbols: List of symbols to generate news for
            headline_interval: Seconds between headlines
            output_dir: Directory to store headline files
        """
        self.generator = HeadlineGenerator(api_key, symbols)
        self.headline_interval = headline_interval
        self.subscribers = []
        self.running = False
        self.thread = None
        self.headlines = []
        
        # Create output directory if it doesn't exist
        os.makedirs(output_dir, exist_ok=True)
        self.output_dir = output_dir
        
        # Create headline log file
        self.headline_log_file = os.path.join(
            output_dir, 
            f"headlines_{datetime.now().strftime('%Y%m%d_%H%M%S')}.json"
        )
        self.headline_latest_file = os.path.join(output_dir, "headlines_latest.json")
        
        print(f"NewsServer initialized with interval {headline_interval} seconds")
        print(f"Headlines will be logged to {self.headline_log_file}")
        print(f"Latest headline will be available at {self.headline_latest_file}")
    
    def start(self):
        """Start the news server"""
        self.running = True
        self.thread = threading.Thread(target=self._run)
        self.thread.daemon = True
        self.thread.start()
        print("================================================")
        print("NEWS SERVER STARTED")
        print("================================================")
        
    def stop(self):
        """Stop the news server"""
        self.running = False
        if self.thread:
            self.thread.join()
        print("================================================")
        print("NEWS SERVER STOPPED")
        print("================================================")
        self._save_headlines()
            
    def _run(self):
        """Main loop for generating headlines"""
        while self.running:
            try:
                # Generate a headline
                headline = self.generator.generate_headline()
                self.headlines.append(headline)
                
                # Log to files
                self._save_headlines()
                self._save_latest_headline(headline)
                
                # Print headline with formatting
                self._print_headline(headline)
                
                # Broadcast to subscribers
                subscriber_count = len(self.subscribers)
                print(f"Broadcasting to {subscriber_count} subscribers...")
                for i, subscriber in enumerate(self.subscribers):
                    print(f"  - Sending to subscriber {i+1}/{subscriber_count}")
                    subscriber(headline)
                    
            except Exception as e:
                print(f"ERROR generating headline: {e}")
                
            # Wait for next interval
            print(f"Waiting {self.headline_interval} seconds until next headline...")
            time.sleep(self.headline_interval)
    
    def _print_headline(self, headline):
        """Print a headline with fancy formatting"""
        sentiment = "POSITIVE" if headline['sentiment'] > 0 else "NEGATIVE"
        impact = headline['impact']
        
        print("\n")
        print("=" * 80)
        print(f"NEWS ALERT - {headline['symbol']} - IMPACT: {impact} - SENTIMENT: {sentiment}")
        print("-" * 80)
        print(f"{headline['headline']}")
        print("=" * 80)
        print("\n")
    
    def _save_headlines(self):
        """Save all headlines to the log file"""
        with open(self.headline_log_file, 'w') as f:
            json.dump(self.headlines, f, indent=2)
    
    def _save_latest_headline(self, headline):
        """Save the most recent headline to a dedicated file"""
        with open(self.headline_latest_file, 'w') as f:
            json.dump(headline, f, indent=2)
    
    def subscribe(self, callback):
        """Add a subscriber to receive headlines"""
        self.subscribers.append(callback)
        subscriber_id = len(self.subscribers) - 1
        print(f"New subscriber registered with ID {subscriber_id}")
        return subscriber_id
        
    def unsubscribe(self, subscriber_id):
        """Remove a subscriber"""
        if subscriber_id < len(self.subscribers):
            self.subscribers.pop(subscriber_id)
            print(f"Unsubscribed subscriber with ID {subscriber_id}")
            return True
        print(f"Failed to unsubscribe - no subscriber with ID {subscriber_id}")
        return False
    
    def get_recent_headlines(self, count=10):
        """Get recent headlines"""
        return self.headlines[-count:]
