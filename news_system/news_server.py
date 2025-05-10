import time
import threading
import json
import os
import random
from datetime import datetime
from headline_generator import HeadlineGenerator
from nlp_sentiment_analyzer import NLPSentimentAnalyzer

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
        self.analyzer = NLPSentimentAnalyzer(api_key)
        self.headline_interval = headline_interval
        self.subscribers = []
        self.running = False
        self.thread = None
        self.headlines = []
        
        # Randomize intervals slightly to make it more realistic
        self.interval_variation = 0.2  # ±20% variation in timing
        
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
                # Determine news category - sometimes generate random headlines
                # to create market noise
                if random.random() < 0.25:  # 25% of headlines are random "noise"
                    category = random.choice(self.generator.news_categories)
                    print(f"Generating random market noise headline in category: {category}")
                else:
                    # Add weight to more impactful categories
                    weighted_categories = (
                        ["earnings", "costs", "demand", "supply"] * 3 +  # 3x weight for fundamental factors
                        ["regulation", "management", "partnership"] * 2 +  # 2x weight for business factors
                        ["product", "competition", "market_share", "international", "innovation", "research"] * 1  # 1x weight for other factors
                    )
                    category = random.choice(weighted_categories)
                    print(f"Generating impactful headline in category: {category}")
                
                # Generate headline with authentic sentiment
                raw_headline = self.generator.generate_headline(category=category)
                
                # Run headline through NLP sentiment analyzer for a more nuanced analysis
                print(f"Analyzing headline sentiment with NLP...")
                nlp_analysis = self.analyzer.analyze(
                    raw_headline["headline"], 
                    raw_headline["symbol"],
                    use_llm=True  # Set to True to use OpenAI API
                )
                
                # Get sentiment score from NLP analysis
                nlp_sentiment_score = nlp_analysis["sentiment_score"]
                confidence = nlp_analysis["confidence"]
                
                # Modify the impact based on NLP analysis and confidence
                # This allows for headlines where the impact doesn't match the original intention
                raw_impact = raw_headline["impact"]
                if abs(nlp_sentiment_score) > 0.3:  # Only adjust if NLP has a strong opinion
                    if (nlp_sentiment_score > 0 and raw_impact < 0) or (nlp_sentiment_score < 0 and raw_impact > 0):
                        # NLP disagrees with original sentiment - adjust impact
                        adjusted_impact = int(nlp_sentiment_score * 10 * confidence)
                        print(f"NLP disagrees with original sentiment. Adjusting impact from {raw_impact} to {adjusted_impact}")
                        raw_headline["impact"] = adjusted_impact
                
                # Add NLP analysis data to headline
                headline = raw_headline.copy()
                headline["nlp_analysis"] = {
                    "sentiment_score": nlp_sentiment_score,
                    "confidence": confidence,
                    "method": nlp_analysis["method"]
                }
                
                # Add original vs analyzed sentiment comparison
                headline["original_sentiment"] = raw_headline["sentiment"]
                headline["analyzed_sentiment_score"] = nlp_sentiment_score
                
                # Add headline to history
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
                
            # Add some randomness to the interval to make it more realistic
            variation = random.uniform(
                1.0 - self.interval_variation, 
                1.0 + self.interval_variation
            )
            wait_time = self.headline_interval * variation
            
            # Wait for next interval
            print(f"Waiting {wait_time:.1f} seconds until next headline...")
            time.sleep(wait_time)
    
    def _print_headline(self, headline):
        """Print a headline with fancy formatting"""
        nlp_score = headline.get("analyzed_sentiment_score", 0)
        original_sentiment = "POSITIVE" if headline['sentiment'] > 0 else "NEGATIVE"
        analyzed_sentiment = "POSITIVE" if nlp_score > 0 else "NEGATIVE"
        impact = headline['impact']
        category = headline.get('category', 'general')
        
        # Format confidence if available
        confidence_str = ""
        if "nlp_analysis" in headline and "confidence" in headline["nlp_analysis"]:
            confidence = headline["nlp_analysis"]["confidence"]
            confidence_str = f" (confidence: {confidence:.2f})"
        
        print("\n")
        print("=" * 80)
        print(f"NEWS ALERT - {headline['symbol']} - CATEGORY: {category.upper()} - IMPACT: {impact}")
        print(f"ORIGINAL SENTIMENT: {original_sentiment} | ANALYZED SENTIMENT: {analyzed_sentiment}{confidence_str}")
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
