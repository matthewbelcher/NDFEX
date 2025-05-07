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
        
        # Categories of financial news, each with positive/negative variants
        self.news_categories = [
            "earnings", "costs", "demand", "supply", "regulation", 
            "management", "product", "competition", "partnership",
            "market_share", "international", "innovation", "research"
        ]
        
        print(f"HeadlineGenerator initialized with symbols: {self.symbols}")
        
    def generate_headline(self, symbol=None, sentiment=None, category=None):
        """
        Generate a financial headline with specific sentiment.
        
        Args:
            symbol: Optional specific symbol to generate news for
            sentiment: Optional sentiment direction (-1 for negative, 1 for positive, None for random)
            category: Optional news category
            
        Returns:
            dict: Contains the headline, metadata, and impact score
        """
        # Select a symbol if not specified
        if symbol is None:
            symbol = random.choice(self.symbols)
            
        # Determine sentiment if not specified
        if sentiment is None:
            sentiment = random.choice([-1, 1])
        
        # Select a category if not specified
        if category is None:
            category = random.choice(self.news_categories)
            
        sentiment_str = "positive" if sentiment > 0 else "negative"    
        print(f"Generating {sentiment_str} {category} headline for {symbol}...")
            
        # Select impact level based on sentiment direction
        if sentiment < 0:
            impact = random.choice([i for i in self.impact_levels if i < 0])
        elif sentiment > 0:
            impact = random.choice([i for i in self.impact_levels if i > 0])
        else:
            impact = 0
            
        # Create prompt for the API
        prompt = self._create_prompt(symbol, sentiment, category)
        print(f"Using prompt: {prompt}")
        
        # Generate headline
        completion = self.client.chat.completions.create(
            model="gpt-4o-mini",
            messages=[
                {"role": "system", "content": "You generate realistic but fictional financial news headlines that require analysis to understand their impact. The headlines should be subtle and not directly state whether they are positive or negative."},
                {"role": "user", "content": prompt}
            ]
        )
        
        headline_text = completion.choices[0].message.content.strip()
        headline_text = headline_text.replace('"', '') # Remove quotation marks if present
        print(f"Generated headline: {headline_text}")
        
        # Create result object
        timestamp = datetime.now().isoformat()
        result = {
            "timestamp": timestamp,
            "symbol": symbol,
            "headline": headline_text,
            "sentiment": sentiment,
            "impact": impact,
            "category": category
        }
        
        print(f"Headline details: Symbol={symbol}, Category={category}, Impact={impact}, Sentiment={sentiment_str}")
        return result
    
    def _create_prompt(self, symbol, sentiment, category):
        """Create a prompt for the API based on desired sentiment, symbol and category"""
        sentiment_desc = "positive" if sentiment > 0 else "negative"
        
        category_prompts = {
            "earnings": {
                "positive": f"Generate a realistic but fictional financial news headline about {symbol}'s earnings that would be considered good news, without directly saying it's positive. The headline should require analysis to understand it's positive.",
                "negative": f"Generate a realistic but fictional financial news headline about {symbol}'s earnings that would be considered bad news, without directly saying it's negative. The headline should require analysis to understand it's negative."
            },
            "costs": {
                "positive": f"Generate a realistic but fictional financial news headline about {symbol}'s costs or expenses that would be considered good news, without directly saying it's positive. The headline should require analysis to understand it's positive.",
                "negative": f"Generate a realistic but fictional financial news headline about {symbol}'s costs or expenses that would be considered bad news, without directly saying it's negative. The headline should require analysis to understand it's negative."
            },
            "demand": {
                "positive": f"Generate a realistic but fictional financial news headline about demand for {symbol}'s products that would be considered good news, without directly saying it's positive. The headline should require analysis to understand it's positive.",
                "negative": f"Generate a realistic but fictional financial news headline about demand for {symbol}'s products that would be considered bad news, without directly saying it's negative. The headline should require analysis to understand it's negative."
            },
            "supply": {
                "positive": f"Generate a realistic but fictional financial news headline about {symbol}'s supply chain that would be considered good news, without directly saying it's positive. The headline should require analysis to understand it's positive.",
                "negative": f"Generate a realistic but fictional financial news headline about {symbol}'s supply chain that would be considered bad news, without directly saying it's negative. The headline should require analysis to understand it's negative."
            },
            "regulation": {
                "positive": f"Generate a realistic but fictional financial news headline about regulations affecting {symbol} that would be considered good news, without directly saying it's positive. The headline should require analysis to understand it's positive.",
                "negative": f"Generate a realistic but fictional financial news headline about regulations affecting {symbol} that would be considered bad news, without directly saying it's negative. The headline should require analysis to understand it's negative."
            },
            "management": {
                "positive": f"Generate a realistic but fictional financial news headline about {symbol}'s management or leadership that would be considered good news, without directly saying it's positive. The headline should require analysis to understand it's positive.",
                "negative": f"Generate a realistic but fictional financial news headline about {symbol}'s management or leadership that would be considered bad news, without directly saying it's negative. The headline should require analysis to understand it's negative."
            },
            "product": {
                "positive": f"Generate a realistic but fictional financial news headline about {symbol}'s products that would be considered good news, without directly saying it's positive. The headline should require analysis to understand it's positive.",
                "negative": f"Generate a realistic but fictional financial news headline about {symbol}'s products that would be considered bad news, without directly saying it's negative. The headline should require analysis to understand it's negative."
            },
            "competition": {
                "positive": f"Generate a realistic but fictional financial news headline about competition in {symbol}'s market that would be considered good news for {symbol}, without directly saying it's positive. The headline should require analysis to understand it's positive.",
                "negative": f"Generate a realistic but fictional financial news headline about competition in {symbol}'s market that would be considered bad news for {symbol}, without directly saying it's negative. The headline should require analysis to understand it's negative."
            },
            "partnership": {
                "positive": f"Generate a realistic but fictional financial news headline about {symbol}'s partnerships or collaborations that would be considered good news, without directly saying it's positive. The headline should require analysis to understand it's positive.",
                "negative": f"Generate a realistic but fictional financial news headline about {symbol}'s partnerships or collaborations that would be considered bad news, without directly saying it's negative. The headline should require analysis to understand it's negative."
            },
            "market_share": {
                "positive": f"Generate a realistic but fictional financial news headline about {symbol}'s market share that would be considered good news, without directly saying it's positive. The headline should require analysis to understand it's positive.",
                "negative": f"Generate a realistic but fictional financial news headline about {symbol}'s market share that would be considered bad news, without directly saying it's negative. The headline should require analysis to understand it's negative."
            },
            "international": {
                "positive": f"Generate a realistic but fictional financial news headline about {symbol}'s international operations that would be considered good news, without directly saying it's positive. The headline should require analysis to understand it's positive.",
                "negative": f"Generate a realistic but fictional financial news headline about {symbol}'s international operations that would be considered bad news, without directly saying it's negative. The headline should require analysis to understand it's negative."
            },
            "innovation": {
                "positive": f"Generate a realistic but fictional financial news headline about {symbol}'s innovation or R&D that would be considered good news, without directly saying it's positive. The headline should require analysis to understand it's positive.",
                "negative": f"Generate a realistic but fictional financial news headline about {symbol}'s innovation or R&D that would be considered bad news, without directly saying it's negative. The headline should require analysis to understand it's negative."
            },
            "research": {
                "positive": f"Generate a realistic but fictional financial news headline about research or analysis of {symbol} that would be considered good news, without directly saying it's positive. The headline should require analysis to understand it's positive.",
                "negative": f"Generate a realistic but fictional financial news headline about research or analysis of {symbol} that would be considered bad news, without directly saying it's negative. The headline should require analysis to understand it's negative."
            }
        }
        
        return category_prompts[category][sentiment_desc]

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
                
            category = random.choice(self.news_categories)
            print(f"Generating headline {i+1}/{count}...")
            headline = self.generate_headline(sentiment=sentiment, category=category)
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
