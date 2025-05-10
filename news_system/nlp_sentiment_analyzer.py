import requests
import json
import os
import time
import openai
from typing import Dict, Any, List, Tuple

class NLPSentimentAnalyzer:
    """
    A more sophisticated sentiment analyzer for financial news headlines
    that uses NLP to determine sentiment and potential market impact.
    """
    
    def __init__(self, api_key=None):
        """
        Initialize the sentiment analyzer.
        
        Args:
            api_key: OpenAI API key for using GPT models for sentiment analysis
        """
        self.api_key = api_key or os.environ.get("OPENAI_API_KEY")
        if self.api_key:
            self.client = openai.OpenAI(api_key=self.api_key)
        
        # Financial term impact dictionaries
        self.positive_terms = {
            "exceed expectations": 0.8,
            "beat estimates": 0.7,
            "record high": 0.8,
            "increase dividend": 0.7,
            "strong demand": 0.6,
            "strategic acquisition": 0.6,
            "cost reduction": 0.5,
            "improve efficiency": 0.5,
            "positive outlook": 0.6,
            "expansion": 0.5,
            "new partnership": 0.5,
            "innovation": 0.6
        }
        
        self.negative_terms = {
            "miss expectations": -0.8,
            "below estimates": -0.7,
            "record low": -0.8,
            "cut dividend": -0.7,
            "weak demand": -0.6,
            "hostile takeover": -0.6,
            "cost increase": -0.5,
            "decreased efficiency": -0.5,
            "negative outlook": -0.6,
            "contraction": -0.5,
            "end partnership": -0.5,
            "layoffs": -0.7
        }
        
        # Industry-specific terms
        self.industry_terms = {
            "commodities": {
                "supply shortage": 0.7,  # Good for producers, bad for consumers
                "oversupply": -0.7,      # Bad for producers, good for consumers
                "production increase": -0.5,
                "production decrease": 0.5,
                "high inventory": -0.6,
                "low inventory": 0.6
            },
            "technology": {
                "new patent": 0.7,
                "patent litigation": -0.6,
                "product launch": 0.7,
                "product delay": -0.7,
                "security breach": -0.8,
                "technology breakthrough": 0.8
            }
        }

    def rule_based_analysis(self, headline: str) -> Dict[str, Any]:
        """
        Analyze sentiment using rule-based approach with financial terminology.
        
        Args:
            headline: The news headline to analyze
            
        Returns:
            Dict containing sentiment score, confidence, and impacted aspects
        """
        headline_lower = headline.lower()
        score = 0.0
        matches = []
        
        # Check for positive terms
        for term, impact in self.positive_terms.items():
            if term in headline_lower:
                score += impact
                matches.append((term, impact))
        
        # Check for negative terms
        for term, impact in self.negative_terms.items():
            if term in headline_lower:
                score += impact
                matches.append((term, impact))
        
        # Check industry-specific terms
        for industry, terms in self.industry_terms.items():
            for term, impact in terms.items():
                if term in headline_lower:
                    score += impact
                    matches.append((term, impact, industry))
        
        # Normalize score to [-1, 1] range
        if score > 1.0:
            score = 1.0
        elif score < -1.0:
            score = -1.0
            
        # Determine confidence level based on number of matches
        if len(matches) > 3:
            confidence = 0.9
        elif len(matches) > 1:
            confidence = 0.7
        elif len(matches) == 1:
            confidence = 0.5
        else:
            confidence = 0.3
            
        return {
            "score": score,
            "confidence": confidence,
            "matches": matches
        }
    
    def llm_based_analysis(self, headline: str, symbol: str) -> Dict[str, Any]:
        """
        Analyze sentiment using OpenAI API for more nuanced understanding.
        
        Args:
            headline: The news headline to analyze
            symbol: The trading symbol the headline is about
            
        Returns:
            Dict containing sentiment score, confidence, and analysis details
        """
        if not self.api_key:
            raise ValueError("OpenAI API key is required for LLM-based analysis")
        
        prompt = f"""
        Analyze the following financial news headline about {symbol} and determine its 
        sentiment and potential market impact. Provide your analysis in the following format:
        
        Headline: "{headline}"
        
        Return a JSON object with these fields:
        - sentiment_score: A number between -1.0 (extremely negative) and 1.0 (extremely positive)
        - confidence: A number between 0 and 1 indicating your confidence in this assessment
        - primary_factors: A list of key factors from the headline that influenced your assessment
        - potential_impact: A brief explanation of how this news might impact the stock price
        - time_frame: The likely duration of this impact (short-term, medium-term, long-term)
        """
        
        try:
            response = self.client.chat.completions.create(
                model="gpt-4o-mini",
                response_format={"type": "json_object"},
                messages=[
                    {"role": "system", "content": "You are a financial analyst specialized in interpreting news sentiment."},
                    {"role": "user", "content": prompt}
                ]
            )
            
            analysis = json.loads(response.choices[0].message.content)
            return analysis
            
        except Exception as e:
            print(f"Error in LLM-based analysis: {e}")
            # Fall back to rule-based analysis
            return self.rule_based_analysis(headline)
    
    def analyze(self, headline: str, symbol: str, use_llm: bool = True) -> Dict[str, Any]:
        """
        Analyze the sentiment of a financial headline.
        
        Args:
            headline: The news headline to analyze
            symbol: The trading symbol the headline is about
            use_llm: Whether to use the LLM-based approach (if API key is available)
            
        Returns:
            Dict containing sentiment score, confidence, and analysis
        """
        # Start with rule-based analysis
        rule_based_result = self.rule_based_analysis(headline)
        
        # If LLM analysis is requested and API key is available, use it
        if use_llm and self.api_key:
            try:
                llm_result = self.llm_based_analysis(headline, symbol)
                
                # Combine the results, giving more weight to LLM for the final score
                combined_score = 0.3 * rule_based_result["score"] + 0.7 * llm_result["sentiment_score"]
                combined_confidence = 0.3 * rule_based_result["confidence"] + 0.7 * llm_result["confidence"]
                
                return {
                    "headline": headline,
                    "symbol": symbol,
                    "sentiment_score": combined_score,
                    "confidence": combined_confidence,
                    "rule_based": rule_based_result,
                    "llm_based": llm_result,
                    "method": "combined"
                }
            except Exception as e:
                print(f"Error combining analysis methods: {e}")
                return {
                    "headline": headline,
                    "symbol": symbol,
                    "sentiment_score": rule_based_result["score"],
                    "confidence": rule_based_result["confidence"],
                    "rule_based": rule_based_result,
                    "method": "rule_based_only" 
                }
        else:
            # Just return rule-based analysis
            return {
                "headline": headline,
                "symbol": symbol,
                "sentiment_score": rule_based_result["score"],
                "confidence": rule_based_result["confidence"],
                "rule_based": rule_based_result,
                "method": "rule_based_only"
            }


# Example usage
if __name__ == "__main__":
    analyzer = NLPSentimentAnalyzer()
    
    # Test with different headlines
    headlines = [
        ("GOLD announces 15% increase in production costs amid supply chain challenges", "GOLD"),
        ("BLUE secures exclusive supplier agreement with major technology partner", "BLUE"),
        ("Analysts lower quarterly expectations for GOLD following industry-wide slowdown", "GOLD"),
        ("BLUE patent application rejected, impact on new product line uncertain", "BLUE")
    ]
    
    for headline, symbol in headlines:
        print(f"\nAnalyzing: {headline}")
        result = analyzer.analyze(headline, symbol, use_llm=False)  # Set to True if you have an API key
        print(f"Sentiment score: {result['sentiment_score']:.2f} (confidence: {result['confidence']:.2f})")
        print(f"Method: {result['method']}")
        if "matches" in result["rule_based"]:
            print("Term matches:")
            for match in result["rule_based"]["matches"]:
                print(f"  - {match}")
