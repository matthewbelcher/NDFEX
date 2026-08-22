"""Generate NDFEX Bot Reference documentation as a PDF."""

from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.lib.colors import HexColor, black, white
from reportlab.lib.units import inch
from reportlab.lib.enums import TA_LEFT, TA_CENTER
from reportlab.platypus import (
    SimpleDocTemplate, Paragraph, Spacer, Table, TableStyle,
    KeepTogether, HRFlowable, Preformatted,
)


def build_pdf(path: str):
    doc = SimpleDocTemplate(
        path,
        pagesize=letter,
        topMargin=0.75 * inch,
        bottomMargin=0.75 * inch,
        leftMargin=0.75 * inch,
        rightMargin=0.75 * inch,
    )

    styles = getSampleStyleSheet()

    # Custom styles
    styles.add(ParagraphStyle(
        "DocTitle", parent=styles["Title"], fontSize=24, spaceAfter=4,
        textColor=HexColor("#1a1a2e"),
    ))
    styles.add(ParagraphStyle(
        "DocSubtitle", parent=styles["Normal"], fontSize=12,
        textColor=HexColor("#666666"), alignment=TA_CENTER, spaceAfter=20,
    ))
    styles.add(ParagraphStyle(
        "SectionHead", parent=styles["Heading1"], fontSize=16,
        textColor=HexColor("#1a1a2e"), spaceBefore=18, spaceAfter=8,
        borderWidth=0, borderPadding=0,
    ))
    styles.add(ParagraphStyle(
        "SubHead", parent=styles["Heading2"], fontSize=13,
        textColor=HexColor("#2d3436"), spaceBefore=12, spaceAfter=6,
    ))
    styles.add(ParagraphStyle(
        "Body", parent=styles["Normal"], fontSize=10, leading=14,
        spaceAfter=6,
    ))
    styles.add(ParagraphStyle(
        "CodeBlock", fontName="Courier", fontSize=9, leading=12,
        leftIndent=16, spaceAfter=8, backColor=HexColor("#f5f5f5"),
        borderWidth=0.5, borderColor=HexColor("#dddddd"),
        borderPadding=6,
    ))
    styles.add(ParagraphStyle(
        "Note", parent=styles["Normal"], fontSize=9, leading=12,
        textColor=HexColor("#555555"), leftIndent=16, spaceAfter=6,
        fontName="Helvetica-Oblique",
    ))
    styles.add(ParagraphStyle(
        "BulletItem", parent=styles["Normal"], fontSize=10, leading=14,
        leftIndent=24, bulletIndent=12, spaceAfter=3,
    ))
    styles.add(ParagraphStyle(
        "FormulaBlock", fontName="Courier-Bold", fontSize=11, leading=14,
        leftIndent=24, spaceAfter=8, spaceBefore=4,
        textColor=HexColor("#1a1a2e"),
    ))
    styles.add(ParagraphStyle(
        "TableCell", fontName="Helvetica", fontSize=9, leading=11,
    ))
    styles.add(ParagraphStyle(
        "TableHeader", fontName="Helvetica-Bold", fontSize=9, leading=11,
        textColor=white,
    ))

    story = []

    def title(text):
        story.append(Paragraph(text, styles["DocTitle"]))

    def subtitle(text):
        story.append(Paragraph(text, styles["DocSubtitle"]))

    def section(text):
        story.append(Paragraph(text, styles["SectionHead"]))

    def subsection(text):
        story.append(Paragraph(text, styles["SubHead"]))

    def body(text):
        story.append(Paragraph(text, styles["Body"]))

    def bullet(text):
        story.append(Paragraph(text, styles["BulletItem"], bulletText="\u2022"))

    def formula(text):
        story.append(Paragraph(text, styles["FormulaBlock"]))

    def code(text):
        story.append(Preformatted(text, styles["CodeBlock"]))

    def note(text):
        story.append(Paragraph(text, styles["Note"]))

    def tc(text):
        return Paragraph(text, styles["TableCell"])

    def th(text):
        return Paragraph(text, styles["TableHeader"])

    def spacer(h=0.15):
        story.append(Spacer(1, h * inch))

    def hr():
        story.append(HRFlowable(
            width="100%", thickness=0.5, color=HexColor("#cccccc"),
            spaceBefore=6, spaceAfter=6,
        ))

    # ── Title ─────────────────────────────────────────────────────────
    spacer(1.0)
    title("NDFEX Bot Reference")
    subtitle("Exchange Bots &amp; Market Simulation")
    spacer(0.3)
    body("This document describes the automated bots that run on the "
         "NDFEX exchange during the trading competition. These bots "
         "provide liquidity, generate price movement, and simulate "
         "realistic market conditions for teams to trade against.")
    spacer(0.2)
    hr()

    # ── Overview ──────────────────────────────────────────────────────
    section("1. Overview")
    body("Two bot processes run concurrently on the exchange, each "
         "logged in as a separate user:")
    spacer(0.1)

    proc_data = [
        [th("Process"), th("User"), th("Role")],
        [tc("bot_runner"), tc("test (ID 99)"),
         tc("Primary market maker \u2014 provides liquidity and "
            "drives price discovery via random-walk fair values")],
        [tc("smarter_bots"), tc("test2 (ID 98)"),
         tc("Reactive trader \u2014 responds to market conditions "
            "with momentum, pressure, and passive order flow")],
    ]
    t = Table(proc_data, colWidths=[1.5 * inch, 1.5 * inch, 3.7 * inch])
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), HexColor("#1a1a2e")),
        ("TEXTCOLOR", (0, 0), (-1, 0), white),
        ("FONTSIZE", (0, 0), (-1, -1), 9),
        ("TOPPADDING", (0, 0), (-1, -1), 5),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
        ("LEFTPADDING", (0, 0), (-1, -1), 6),
        ("GRID", (0, 0), (-1, -1), 0.5, HexColor("#cccccc")),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [white, HexColor("#f9f9f9")]),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t)
    spacer(0.1)
    body("Both processes trade all 13 symbols. Together they create a "
         "market with continuous two-sided quotes, random price movement, "
         "and periodic aggressive order flow.")

    hr()

    # ══════════════════════════════════════════════════════════════════
    # BOT_RUNNER BOTS
    # ══════════════════════════════════════════════════════════════════
    section("2. bot_runner Bots")
    body("The bot_runner process contains three bot types that work "
         "together to form the core market structure.")

    # ── RandomWalkFairValue ───────────────────────────────────────────
    subsection("2.1 RandomWalkFairValue")
    body("Not a bot itself, but the price engine that drives the market "
         "makers. Each of the 12 standalone symbols (GOLD, BLUE, and the "
         "10 dorms) has its own independent random walk that evolves over "
         "time.")
    spacer(0.1)
    body("<b>How it works:</b>")
    bullet("Starts at a configured initial price (e.g. GOLD at 1200, "
           "BLUE at 900, dorms in the 500\u2013600 range).")
    bullet("At random intervals (1\u2013100ms), the fair value moves up "
           "or down by a small random step (bounded by tick size).")
    bullet("The value is clamped to stay within the symbol's min/max "
           "price range.")
    body("The UNDY ETF uses a separate <b>BasketFairValue</b> that "
         "computes its fair value as the sum of the 10 dorm component "
         "mid-prices from the live order book, just like a real ETF "
         "arbitrageur would.")

    hr()

    # ── FairValueMarketMaker ──────────────────────────────────────────
    subsection("2.2 FairValueMarketMaker")
    body("The primary liquidity provider. Seven instances run "
         "simultaneously, each with a different \"variance\" "
         "(bias) applied to the fair value, creating a spread "
         "of opinions in the market.")
    spacer(0.1)
    body("<b>How it works:</b>")
    bullet("Computes a biased fair value: "
           "<font face='Courier'>fv = random_walk_fv + variance</font>.")
    bullet("Applies position-based skew: the fair value is adjusted "
           "downward by the bot's current position (up to \u00b1100), "
           "so large positions push quotes away to reduce inventory.")
    bullet("Places a resting bid and ask a configurable number of ticks "
           "wide of the skewed fair value.")
    bullet("Cancels and replaces orders when the fair value moves or "
           "after 30 seconds, whichever comes first.")
    spacer(0.1)

    body("<b>The seven market maker personalities:</b>")
    var_data = [
        [th("Instance"), th("Variance Profile"), th("Width"), th("Qty")],
        [tc("1"), tc("Neutral (all zeros)"), tc("2 ticks"), tc("100")],
        [tc("2"), tc("Slightly bullish (+1 all symbols)"), tc("2 ticks"), tc("90")],
        [tc("3"), tc("Slightly bearish (\u22121 all symbols)"), tc("2 ticks"), tc("80")],
        [tc("4"), tc("Mixed (+2/0/+1 alternating)"), tc("3 ticks"), tc("70")],
        [tc("5"), tc("Alternating (+2/0 pattern)"), tc("3 ticks"), tc("60")],
        [tc("6"), tc("Dorm focused (+2 dorms, 0 GOLD)"), tc("3 ticks"), tc("50")],
        [tc("7"), tc("Contrarian (+3 GOLD, \u00b11 dorms)"), tc("3 ticks"), tc("40")],
    ]
    t = Table(var_data, colWidths=[0.8 * inch, 2.7 * inch, 0.9 * inch, 0.6 * inch])
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), HexColor("#1a1a2e")),
        ("TEXTCOLOR", (0, 0), (-1, 0), white),
        ("FONTSIZE", (0, 0), (-1, -1), 9),
        ("TOPPADDING", (0, 0), (-1, -1), 4),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
        ("LEFTPADDING", (0, 0), (-1, -1), 6),
        ("GRID", (0, 0), (-1, -1), 0.5, HexColor("#cccccc")),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [white, HexColor("#f9f9f9")]),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t)
    spacer(0.1)
    note("All UNDY variances are 0 \u2014 the ETF is always quoted at "
         "basket fair value without bias.")

    hr()

    # ── FairValueStackingMarketMaker ──────────────────────────────────
    subsection("2.3 FairValueStackingMarketMaker")
    body("A depth-of-book liquidity provider that adds resting orders "
         "at multiple price levels behind the best bid/ask. One instance "
         "runs per symbol (13 total).")
    spacer(0.1)
    body("<b>How it works:</b>")
    bullet("Computes the <b>weighted mid price</b> from the current "
           "best bid/ask and their quantities.")
    bullet("Uses a <b>StackManager</b> to maintain a stack of up to "
           "10 resting orders on each side, spaced 1 tick apart.")
    bullet("Orders are placed at 1 tick wide of the weighted mid, "
           "with 2 lots each.")
    bullet("As the mid moves, the stack re-centers: stale levels are "
           "cancelled and new levels are added.")
    body("This creates realistic depth in the order book beyond "
         "the top of book.")

    hr()

    # ── RandomTaker ───────────────────────────────────────────────────
    subsection("2.4 RandomTaker")
    body("Simulates uninformed aggressive order flow. Four instances "
         "run with different quantities and timing scales.")
    spacer(0.1)
    body("<b>How it works:</b>")
    bullet("At random intervals, sends <b>IOC</b> (Immediate or Cancel) "
           "orders that cross the spread to trade immediately.")
    bullet("Each tick, randomly chooses buy or sell for every symbol.")
    bullet("Buys at the best ask price; sells at the best bid price.")
    spacer(0.1)

    taker_data = [
        [th("Instance"), th("Quantity"), th("Interval Range")],
        [tc("1"), tc("1 lot"), tc("60\u20133,000 ms")],
        [tc("2"), tc("2 lots"), tc("70\u20133,500 ms")],
        [tc("3"), tc("3 lots"), tc("80\u20134,000 ms")],
        [tc("4"), tc("4 lots"), tc("90\u20134,500 ms")],
    ]
    t = Table(taker_data, colWidths=[1.0 * inch, 1.5 * inch, 2.5 * inch])
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), HexColor("#1a1a2e")),
        ("TEXTCOLOR", (0, 0), (-1, 0), white),
        ("FONTSIZE", (0, 0), (-1, -1), 9),
        ("TOPPADDING", (0, 0), (-1, -1), 4),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
        ("LEFTPADDING", (0, 0), (-1, -1), 6),
        ("GRID", (0, 0), (-1, -1), 0.5, HexColor("#cccccc")),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [white, HexColor("#f9f9f9")]),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t)
    spacer(0.1)
    note("RandomTakers generate volume and trigger fills against the "
         "market makers' resting orders, creating trade flow that moves "
         "prices.")

    hr()

    # ══════════════════════════════════════════════════════════════════
    # SMARTER_BOTS
    # ══════════════════════════════════════════════════════════════════
    section("3. smarter_bots")
    body("The smarter_bots process runs three reactive bot types that "
         "respond to live market conditions rather than following a "
         "random walk. These bots create momentum, directional pressure, "
         "and passive order flow.")

    # ── ImbalanceTaker ────────────────────────────────────────────────
    subsection("3.1 ImbalanceTaker")
    body("A momentum-following bot that detects order flow imbalance "
         "and trades in the direction of the dominant side.")
    spacer(0.1)
    body("<b>How it works:</b>")
    bullet("Listens to the trade summary feed and tracks a running "
           "imbalance counter per symbol: buy-side volume increments "
           "the counter, sell-side decrements it.")
    bullet("When the accumulated imbalance exceeds a per-symbol "
           "threshold, the bot sends an aggressive order in the "
           "direction of the imbalance (buy if imbalance is positive, "
           "sell if negative).")
    bullet("After firing, the imbalance counter for that symbol resets "
           "to zero.")
    bullet("A 100ms cooldown prevents rapid-fire orders.")
    spacer(0.1)

    imb_data = [
        [th("Parameter"), th("Value")],
        [tc("Order quantity"), tc("40 lots per trigger")],
        [tc("Cooldown"), tc("100 ms between orders")],
        [tc("Order type"), tc("Aggressive (max/min price to ensure fill)")],
    ]
    t = Table(imb_data, colWidths=[2.0 * inch, 4.0 * inch])
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), HexColor("#1a1a2e")),
        ("TEXTCOLOR", (0, 0), (-1, 0), white),
        ("FONTSIZE", (0, 0), (-1, -1), 9),
        ("TOPPADDING", (0, 0), (-1, -1), 4),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
        ("LEFTPADDING", (0, 0), (-1, -1), 6),
        ("GRID", (0, 0), (-1, -1), 0.5, HexColor("#cccccc")),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [white, HexColor("#f9f9f9")]),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t)
    spacer(0.1)

    body("<b>Imbalance thresholds by symbol:</b>")
    thresh_data = [
        [th("Symbol"), th("GOLD"), th("BLUE"), th("KNAN"), th("STED"),
         th("FISH"), th("DILN"), th("SORN"), th("RYAN"), th("LYON"),
         th("WLSH"), th("LEWI"), th("BDIN"), th("UNDY")],
        [tc("Threshold"), tc("200"), tc("400"), tc("300"), tc("300"),
         tc("300"), tc("300"), tc("300"), tc("300"), tc("300"),
         tc("300"), tc("300"), tc("300"), tc("250")],
    ]
    t = Table(thresh_data, colWidths=[0.6 * inch] + [0.44 * inch] * 13)
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), HexColor("#1a1a2e")),
        ("TEXTCOLOR", (0, 0), (-1, 0), white),
        ("FONTSIZE", (0, 0), (-1, -1), 8),
        ("TOPPADDING", (0, 0), (-1, -1), 3),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 3),
        ("LEFTPADDING", (0, 0), (-1, -1), 3),
        ("GRID", (0, 0), (-1, -1), 0.5, HexColor("#cccccc")),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t)
    spacer(0.1)
    note("GOLD has the lowest threshold (200) making it most responsive "
         "to imbalance. BLUE has the highest (400), requiring larger "
         "one-sided flow before the bot reacts.")

    hr()

    # ── PressureTaker ─────────────────────────────────────────────────
    subsection("3.2 PressureTaker")
    body("Simulates directional pressure by sending a burst of "
         "aggressive orders on one side of the market, pushing "
         "prices in a single direction.")
    spacer(0.1)
    body("<b>How it works:</b>")
    bullet("Periodically loads a random \"pressure campaign\": a random "
           "quantity (1\u2013100 lots) and a random direction (buy or sell).")
    bullet("Sends <b>IOC</b> orders of 5 lots across all 13 symbols "
           "on the chosen side, repeatedly, until the pressure quantity "
           "is exhausted.")
    bullet("Each burst is separated by a random delay of "
           "200ms\u201310 seconds.")
    bullet("Buys hit the best ask; sells hit the best bid. If no "
           "liquidity exists on the target side, that symbol is skipped.")
    spacer(0.1)
    body("The effect is a sustained push in one direction across all "
         "symbols simultaneously, followed by a quiet period, then a "
         "new random direction. This creates momentum-style moves that "
         "teams can detect and trade around.")

    hr()

    # ── RestingOrders ─────────────────────────────────────────────────
    subsection("3.3 RestingOrders")
    body("Simulates a passive participant that places limit orders "
         "away from the current market, adding depth to the book.")
    spacer(0.1)
    body("<b>How it works:</b>")
    bullet("At random intervals (500ms\u201310 seconds), picks a "
           "random side (buy or sell).")
    bullet("Places resting orders on that side across all 13 symbols.")
    bullet("Buy orders are placed <b>3 ticks below</b> the current "
           "best bid; sell orders are placed <b>3 ticks above</b> the "
           "current best ask.")
    bullet("Each order is for 25 lots.")
    bullet("Orders are fire-and-forget \u2014 they are never cancelled "
           "by this bot, so they accumulate in the book as passive "
           "liquidity that can be traded against.")
    spacer(0.1)
    note("These resting orders create opportunities for teams: they "
         "sit away from the current price and can be picked off if "
         "the market moves toward them.")

    hr()

    # ── How Bots Interact ─────────────────────────────────────────────
    section("4. How the Bots Interact")
    body("The two bot processes create a self-sustaining market "
         "ecosystem:")
    spacer(0.1)
    bullet("<b>FairValueMarketMaker</b> provides continuous two-sided "
           "quotes that track the random walk, establishing the "
           "\"fair\" price for each symbol.")
    bullet("<b>FairValueStackingMarketMaker</b> adds depth behind "
           "the top-of-book, making the order book realistic.")
    bullet("<b>RandomTaker</b> generates baseline trade flow by "
           "crossing the spread, causing fills and price prints.")
    bullet("<b>ImbalanceTaker</b> amplifies momentum: when one-sided "
           "flow accumulates, it piles on, creating short-term trends.")
    bullet("<b>PressureTaker</b> creates sustained directional moves "
           "across all symbols at once.")
    bullet("<b>RestingOrders</b> seeds the book with passive liquidity "
           "away from the market, creating depth and fill opportunities "
           "for teams.")

    hr()

    # ── What This Means for Teams ─────────────────────────────────────
    section("5. What This Means for Teams")
    bullet("<b>There is always liquidity.</b> The market makers ensure "
           "continuous two-sided quotes in all symbols.")
    bullet("<b>Prices move.</b> The random walk, taker bots, and "
           "pressure bursts create genuine price action.")
    bullet("<b>Momentum is real.</b> The ImbalanceTaker and "
           "PressureTaker create short-term trends you can detect.")
    bullet("<b>Depth exists away from the market.</b> RestingOrders "
           "place passive orders 3 ticks out that can be filled if "
           "the market moves.")
    bullet("<b>The ETF tracks its components.</b> UNDY quotes are "
           "derived from live dorm mid-prices, so ETF arbitrage "
           "opportunities arise when dorm prices move.")
    bullet("<b>Spreads vary.</b> The seven market makers have "
           "different widths and biases, so the effective spread "
           "varies by symbol and over time.")

    # Build
    doc.build(story)
    print(f"PDF written to {path}")


if __name__ == "__main__":
    build_pdf("/home/matt/NDFEX/docs/bot_reference.pdf")
