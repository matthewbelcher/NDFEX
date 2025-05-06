import os
import random
import time
import json
import openai
from datetime import datetime

class HeadlineGenerator:
    def __init__(self, api_key, symbols=None):
        self.client = openai.OpenAI(api_key=api_key)
        self.symbols = symbols or ["GOLD", "BLUE"]  # Default to your exchange symbols
        self.impact_levels = [-10, -5, -3, -1, 0, 1, 3, 5, 10]
        print(f"HeadlineGenerator initialized with symbols: {self.symbols}")
        
    def generate_headline(self, symbol=None, sentiment=None):
        """
        Generate a financial headline with specific sentiment.
        
        Args:
            symbol: Optional specific symbol to generate news for
            sentiment: Optional sentiment direction (-1 for negative, 1 for positive, None for random)
            
        Returns:
            dict: Contains the headline, metadata, and impact score
        """
        # Select a symbol if not specified
        if symbol is None:
            symbol = random.choice(self.symbols)
            
        # Determine sentiment if not specified
        if sentiment is None:
            sentiment = random.choice([-1, 1])
        
        sentiment_str = "positive" if sentiment > 0 else "negative"    
        print(f"Generating {sentiment_str} headline for {symbol}...")
            
        # Select impact level based on sentiment direction
        if sentiment < 0:
            impact = random.choice([i for i in self.impact_levels if i < 0])
        elif sentiment > 0:
            impact = random.choice([i for i in self.impact_levels if i > 0])
        else:
            impact = 0
            
        # Create prompt for the API
        prompt = self._create_prompt(symbol, sentiment)
        print(f"Using prompt: {prompt}")
        
        # Generate headline
        completion = self.client.chat.completions.create(
            model="gpt-4o-mini",
            messages=[
                {"role": "system", "content": "You generate realistic but fictional financial news headlines."},
                {"role": "user", "content": prompt}
            ]
        )
        
        headline_text = completion.choices[0].message.content.strip()
        print(f"Generated headline: {headline_text}")
        
        # Create result object
        timestamp = datetime.now().isoformat()
        result = {
            "timestamp": timestamp,
            "symbol": symbol,
            "headline": headline_text,
            "sentiment": sentiment,
            "impact": impact,
        }
        
        print(f"Headline details: Symbol={symbol}, Impact={impact}, Sentiment={sentiment_str}")
        return result
    
    def _create_prompt(self, symbol, sentiment):
        """Create a prompt for the API based on desired sentiment and symbol"""
        sentiment_desc = "positive" if sentiment > 0 else "negative"
        
        prompts = {
            "positive": [
                f"Generate a realistic but fictional positive financial news headline about {symbol}.",
                f"Create a bullish news headline for {symbol} that would cause traders to buy.",
                f"Write a financial headline suggesting growth or positive developments for {symbol}."
            ],
            "negative": [
                f"Generate a realistic but fictional negative financial news headline about {symbol}.",
                f"Create a bearish news headline for {symbol} that would cause traders to sell.",
                f"Write a financial headline suggesting problems or negative developments for {symbol}."
            ]
        }
        
        return random.choice(prompts[sentiment_desc])

    def generate_headline_batch(self, count=5, mixed=True):
        """Generate multiple headlines, optionally with mixed sentiment"""
        print(f"Generating batch of {count} headlines (mixed={mixed})...")
        results = []
        
        for i in range(count):
            # If mixed, alternate sentiment
            if mixed:
                sentiment = random.choice([-1, 1])
            else:
                sentiment = None  # Random
                
            print(f"Generating headline {i+1}/{count}...")
            headline = self.generate_headline(sentiment=sentiment)
            results.append(headline)
            time.sleep(0.5)  # Avoid rate limits
            
        print(f"Generated {len(results)} headlines")
        return results
    
    def save_headlines(self, headlines, filename="headlines.json"):
        """Save headlines to a JSON file"""
        with open(filename, 'w') as f:
            json.dump(headlines, f, indent=2)
            
        print(f"Saved {len(headlines)} headlines to {filename}")
        return filename
