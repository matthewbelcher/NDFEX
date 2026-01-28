#!/usr/bin/env python3
"""
Automated tests for the NDFEX Homework Checker.

These tests work with live market data by capturing state at specific
sequence numbers and validating submissions against those snapshots.

Usage:
    uv run test_homework_checker.py [--host HOST] [--port PORT]
"""

import argparse
import json
import random
import string
import sys
import time
import urllib.request
import urllib.error
from dataclasses import dataclass
from typing import Optional, Tuple


@dataclass
class TestResult:
    name: str
    passed: bool
    message: str
    duration_ms: float


class HomeworkCheckerTester:
    def __init__(self, host: str = "localhost", port: int = 8080):
        self.base_url = f"http://{host}:{port}"
        self.results: list[TestResult] = []
        # Generate unique student ID for this test run to avoid score pollution
        self.test_student_id = f"test_{''.join(random.choices(string.ascii_lowercase, k=8))}"

    def _request(self, method: str, endpoint: str, data: Optional[dict] = None) -> Tuple[int, dict]:
        """Make HTTP request and return (status_code, response_dict)"""
        url = f"{self.base_url}{endpoint}"
        headers = {"Content-Type": "application/json"} if data else {}

        req = urllib.request.Request(
            url,
            data=json.dumps(data).encode() if data else None,
            headers=headers,
            method=method
        )

        try:
            with urllib.request.urlopen(req, timeout=10) as response:
                return response.status, json.loads(response.read().decode())
        except urllib.error.HTTPError as e:
            return e.code, json.loads(e.read().decode())
        except urllib.error.URLError as e:
            raise ConnectionError(f"Cannot connect to {url}: {e}")

    def _get(self, endpoint: str) -> Tuple[int, dict]:
        return self._request("GET", endpoint, None)

    def _post(self, endpoint: str, data: dict) -> Tuple[int, dict]:
        return self._request("POST", endpoint, data)

    def _run_test(self, name: str, test_func) -> TestResult:
        """Run a single test and record result"""
        start = time.time()
        try:
            passed, message = test_func()
        except Exception as e:
            passed, message = False, f"Exception: {e}"
        duration_ms = (time.time() - start) * 1000

        result = TestResult(name, passed, message, duration_ms)
        self.results.append(result)
        return result

    def get_current_state(self) -> dict:
        """Get current market state from the checker"""
        status, data = self._get("/current")
        if status != 200:
            raise RuntimeError(f"Failed to get current state: {data}")
        return data

    # ==================== Test Cases ====================

    def test_health_endpoint(self) -> Tuple[bool, str]:
        """Test that health endpoint returns correct structure"""
        status, data = self._get("/health")

        if status != 200:
            return False, f"Expected status 200, got {status}"

        required_fields = ["status", "ws_connected", "current_seq_num", "last_update"]
        missing = [f for f in required_fields if f not in data]
        if missing:
            return False, f"Missing fields: {missing}"

        if data["status"] != "ok":
            return False, f"Status is not 'ok': {data['status']}"

        if not data["ws_connected"]:
            return False, "WebSocket not connected"

        return True, f"Health OK, seq_num={data['current_seq_num']}"

    def test_current_endpoint(self) -> Tuple[bool, str]:
        """Test that current endpoint returns market state with seq_num"""
        status, data = self._get("/current")

        if status != 200:
            return False, f"Expected status 200, got {status}"

        if "current_seq_num" not in data:
            return False, "Missing current_seq_num"

        if "symbols" not in data:
            return False, "Missing symbols"

        for symbol_id in ["1", "2"]:
            if symbol_id not in data["symbols"]:
                return False, f"Missing symbol {symbol_id}"

            symbol = data["symbols"][symbol_id]
            required = ["seq_num", "best_bid_price", "best_bid_qty", "best_ask_price", "best_ask_qty"]
            missing = [f for f in required if f not in symbol]
            if missing:
                return False, f"Symbol {symbol_id} missing fields: {missing}"

        return True, f"Current state OK, seq_num={data['current_seq_num']}"

    def test_correct_bbo_submission(self) -> Tuple[bool, str]:
        """Test that a correct BBO submission is accepted"""
        state = self.get_current_state()
        seq_num = state["current_seq_num"]
        symbol = state["symbols"]["1"]

        submission = {
            "student_id": self.test_student_id,
            "seq_num": seq_num,
            "symbol": 1,
            "best_bid_price": symbol["best_bid_price"],
            "best_bid_qty": symbol["best_bid_qty"],
            "best_ask_price": symbol["best_ask_price"],
            "best_ask_qty": symbol["best_ask_qty"]
        }

        status, data = self._post("/submit/bbo", submission)

        if status != 200:
            return False, f"Expected status 200, got {status}: {data}"

        if not data.get("correct"):
            return False, f"Expected correct=true, got: {data.get('message')}"

        if data.get("message") != "Correct":
            return False, f"Expected message='Correct', got: {data.get('message')}"

        return True, f"Correct submission accepted at seq_num={seq_num}"

    def test_incorrect_bid_price(self) -> Tuple[bool, str]:
        """Test that incorrect bid price is rejected with proper message"""
        state = self.get_current_state()
        seq_num = state["current_seq_num"]
        symbol = state["symbols"]["1"]

        wrong_price = symbol["best_bid_price"] + 9999

        submission = {
            "student_id": self.test_student_id,
            "seq_num": seq_num,
            "symbol": 1,
            "best_bid_price": wrong_price,
            "best_bid_qty": symbol["best_bid_qty"],
            "best_ask_price": symbol["best_ask_price"],
            "best_ask_qty": symbol["best_ask_qty"]
        }

        status, data = self._post("/submit/bbo", submission)

        if status != 200:
            return False, f"Expected status 200, got {status}"

        if data.get("correct"):
            return False, "Expected correct=false for wrong bid price"

        message = data.get("message", "")
        if "Incorrect" not in message:
            return False, f"Expected 'Incorrect' in message: {message}"

        # Verify qty@price format (not price@qty)
        expected_bid_str = f"bid={symbol['best_bid_qty']}@{symbol['best_bid_price']}"
        if expected_bid_str not in message:
            return False, f"Expected '{expected_bid_str}' in message (qty@price format): {message}"

        return True, f"Incorrect bid price rejected with proper format"

    def test_incorrect_ask_qty(self) -> Tuple[bool, str]:
        """Test that incorrect ask quantity is rejected"""
        state = self.get_current_state()
        seq_num = state["current_seq_num"]
        symbol = state["symbols"]["1"]

        wrong_qty = symbol["best_ask_qty"] + 1000

        submission = {
            "student_id": self.test_student_id,
            "seq_num": seq_num,
            "symbol": 1,
            "best_bid_price": symbol["best_bid_price"],
            "best_bid_qty": symbol["best_bid_qty"],
            "best_ask_price": symbol["best_ask_price"],
            "best_ask_qty": wrong_qty
        }

        status, data = self._post("/submit/bbo", submission)

        if status != 200:
            return False, f"Expected status 200, got {status}"

        if data.get("correct"):
            return False, "Expected correct=false for wrong ask qty"

        # Verify the got values show the wrong qty
        message = data.get("message", "")
        got_ask_str = f"ask={wrong_qty}@{symbol['best_ask_price']}"
        if got_ask_str not in message:
            return False, f"Expected '{got_ask_str}' in message: {message}"

        return True, "Incorrect ask qty rejected with proper format"

    def test_future_seq_num(self) -> Tuple[bool, str]:
        """Test that future sequence numbers are rejected"""
        state = self.get_current_state()
        future_seq = state["current_seq_num"] + 1000000
        symbol = state["symbols"]["1"]

        submission = {
            "student_id": self.test_student_id,
            "seq_num": future_seq,
            "symbol": 1,
            "best_bid_price": symbol["best_bid_price"],
            "best_bid_qty": symbol["best_bid_qty"],
            "best_ask_price": symbol["best_ask_price"],
            "best_ask_qty": symbol["best_ask_qty"]
        }

        status, data = self._post("/submit/bbo", submission)

        if status != 200:
            return False, f"Expected status 200, got {status}"

        if data.get("correct"):
            return False, "Expected correct=false for future seq_num"

        message = data.get("message", "")
        if "in the future" not in message:
            return False, f"Expected 'in the future' in message: {message}"

        return True, "Future seq_num rejected correctly"

    def test_old_seq_num(self) -> Tuple[bool, str]:
        """Test that very old sequence numbers are rejected"""
        state = self.get_current_state()
        symbol = state["symbols"]["1"]

        submission = {
            "student_id": self.test_student_id,
            "seq_num": 1,  # Very old
            "symbol": 1,
            "best_bid_price": symbol["best_bid_price"],
            "best_bid_qty": symbol["best_bid_qty"],
            "best_ask_price": symbol["best_ask_price"],
            "best_ask_qty": symbol["best_ask_qty"]
        }

        status, data = self._post("/submit/bbo", submission)

        if status != 200:
            return False, f"Expected status 200, got {status}"

        if data.get("correct"):
            return False, "Expected correct=false for old seq_num"

        message = data.get("message", "")
        if "too old" not in message:
            return False, f"Expected 'too old' in message: {message}"

        return True, "Old seq_num rejected correctly"

    def test_missing_fields(self) -> Tuple[bool, str]:
        """Test that missing required fields return error"""
        submission = {
            "student_id": self.test_student_id,
            "symbol": 1
            # Missing seq_num, prices, quantities
        }

        status, data = self._post("/submit/bbo", submission)

        if status != 400:
            return False, f"Expected status 400, got {status}"

        if "Missing fields" not in data.get("error", ""):
            return False, f"Expected 'Missing fields' error: {data}"

        return True, "Missing fields error returned correctly"

    def test_invalid_json(self) -> Tuple[bool, str]:
        """Test that invalid JSON returns error"""
        url = f"{self.base_url}/submit/bbo"
        req = urllib.request.Request(
            url,
            data=b"not valid json",
            headers={"Content-Type": "application/json"},
            method="POST"
        )

        try:
            with urllib.request.urlopen(req, timeout=10) as response:
                status = response.status
                data = json.loads(response.read().decode())
        except urllib.error.HTTPError as e:
            status = e.code
            data = json.loads(e.read().decode())

        if status != 400:
            return False, f"Expected status 400, got {status}"

        if "Invalid JSON" not in data.get("error", ""):
            return False, f"Expected 'Invalid JSON' error: {data}"

        return True, "Invalid JSON error returned correctly"

    def test_symbol_2(self) -> Tuple[bool, str]:
        """Test submission for symbol 2 (BLUE)"""
        state = self.get_current_state()
        seq_num = state["current_seq_num"]
        symbol = state["symbols"]["2"]

        submission = {
            "student_id": self.test_student_id,
            "seq_num": seq_num,
            "symbol": 2,
            "best_bid_price": symbol["best_bid_price"],
            "best_bid_qty": symbol["best_bid_qty"],
            "best_ask_price": symbol["best_ask_price"],
            "best_ask_qty": symbol["best_ask_qty"]
        }

        status, data = self._post("/submit/bbo", submission)

        if status != 200:
            return False, f"Expected status 200, got {status}"

        if not data.get("correct"):
            return False, f"Expected correct=true for symbol 2: {data.get('message')}"

        return True, f"Symbol 2 submission accepted at seq_num={seq_num}"

    def test_score_tracking(self) -> Tuple[bool, str]:
        """Test that scores are tracked correctly"""
        # Make a unique student for this test
        student_id = f"score_test_{''.join(random.choices(string.ascii_lowercase, k=6))}"

        # Get current state and make one correct submission
        state = self.get_current_state()
        seq_num = state["current_seq_num"]
        symbol = state["symbols"]["1"]

        submission = {
            "student_id": student_id,
            "seq_num": seq_num,
            "symbol": 1,
            "best_bid_price": symbol["best_bid_price"],
            "best_bid_qty": symbol["best_bid_qty"],
            "best_ask_price": symbol["best_ask_price"],
            "best_ask_qty": symbol["best_ask_qty"]
        }

        status, data = self._post("/submit/bbo", submission)
        if status != 200 or not data.get("correct"):
            return False, f"First submission failed: {data}"

        # Check score in response
        score = data.get("score", {})
        if score.get("bbo_correct") != 1:
            return False, f"Expected bbo_correct=1, got {score.get('bbo_correct')}"

        # Make an incorrect submission
        submission["best_bid_price"] = 0  # Wrong price
        status, data = self._post("/submit/bbo", submission)
        if status != 200:
            return False, f"Second submission failed: {data}"

        score = data.get("score", {})
        if score.get("bbo_correct") != 1:
            return False, f"Expected bbo_correct still 1, got {score.get('bbo_correct')}"
        if score.get("bbo_incorrect") != 1:
            return False, f"Expected bbo_incorrect=1, got {score.get('bbo_incorrect')}"

        # Check status endpoint
        status, data = self._get(f"/status/{student_id}")
        if status != 200:
            return False, f"Status endpoint failed: {data}"

        if data.get("bbo_correct") != 1 or data.get("bbo_incorrect") != 1:
            return False, f"Status endpoint scores don't match: {data}"

        return True, "Score tracking works correctly"

    def test_leaderboard(self) -> Tuple[bool, str]:
        """Test leaderboard endpoint"""
        status, data = self._get("/leaderboard")

        if status != 200:
            return False, f"Expected status 200, got {status}"

        if "students" not in data:
            return False, "Missing 'students' in response"

        if "required_correct" not in data:
            return False, "Missing 'required_correct' in response"

        # Verify structure of student entries
        if len(data["students"]) > 0:
            student = data["students"][0]
            required = ["student_id", "bbo_correct", "bbo_incorrect"]
            missing = [f for f in required if f not in student]
            if missing:
                return False, f"Student entry missing fields: {missing}"

        return True, f"Leaderboard OK with {len(data['students'])} students"

    def test_rapid_submissions(self) -> Tuple[bool, str]:
        """Test that rapid submissions at same seq_num work correctly"""
        state = self.get_current_state()
        seq_num = state["current_seq_num"]

        correct_count = 0
        for symbol_id in ["1", "2"]:
            symbol = state["symbols"][symbol_id]
            submission = {
                "student_id": self.test_student_id,
                "seq_num": seq_num,
                "symbol": int(symbol_id),
                "best_bid_price": symbol["best_bid_price"],
                "best_bid_qty": symbol["best_bid_qty"],
                "best_ask_price": symbol["best_ask_price"],
                "best_ask_qty": symbol["best_ask_qty"]
            }

            status, data = self._post("/submit/bbo", submission)
            if status == 200 and data.get("correct"):
                correct_count += 1

        if correct_count != 2:
            return False, f"Expected 2 correct submissions, got {correct_count}"

        return True, "Rapid submissions at same seq_num work"

    def run_all_tests(self) -> bool:
        """Run all tests and return overall success"""
        tests = [
            ("Health endpoint", self.test_health_endpoint),
            ("Current endpoint", self.test_current_endpoint),
            ("Correct BBO submission", self.test_correct_bbo_submission),
            ("Incorrect bid price", self.test_incorrect_bid_price),
            ("Incorrect ask qty", self.test_incorrect_ask_qty),
            ("Future seq_num", self.test_future_seq_num),
            ("Old seq_num", self.test_old_seq_num),
            ("Missing fields", self.test_missing_fields),
            ("Invalid JSON", self.test_invalid_json),
            ("Symbol 2 (BLUE)", self.test_symbol_2),
            ("Score tracking", self.test_score_tracking),
            ("Leaderboard", self.test_leaderboard),
            ("Rapid submissions", self.test_rapid_submissions),
        ]

        print(f"Running {len(tests)} tests against {self.base_url}")
        print(f"Test student ID: {self.test_student_id}")
        print("=" * 60)

        for name, test_func in tests:
            result = self._run_test(name, test_func)
            status = "PASS" if result.passed else "FAIL"
            print(f"[{status}] {result.name} ({result.duration_ms:.1f}ms)")
            if not result.passed:
                print(f"       {result.message}")

        print("=" * 60)
        passed = sum(1 for r in self.results if r.passed)
        failed = sum(1 for r in self.results if not r.passed)
        total_time = sum(r.duration_ms for r in self.results)

        print(f"Results: {passed} passed, {failed} failed ({total_time:.1f}ms total)")

        return failed == 0


def main():
    parser = argparse.ArgumentParser(description="Test the NDFEX Homework Checker")
    parser.add_argument("--host", default="localhost", help="Homework checker host")
    parser.add_argument("--port", type=int, default=8080, help="Homework checker port")
    args = parser.parse_args()

    tester = HomeworkCheckerTester(host=args.host, port=args.port)

    try:
        success = tester.run_all_tests()
        sys.exit(0 if success else 1)
    except ConnectionError as e:
        print(f"ERROR: {e}")
        print("Make sure the homework checker server is running.")
        sys.exit(1)


if __name__ == "__main__":
    main()
