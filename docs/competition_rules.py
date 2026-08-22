"""Generate NDFEX Trading Competition Rules as a PDF."""

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
    title("NDFEX Trading Competition")
    subtitle("Rules, Penalties &amp; Scoreboard Guide")
    spacer(0.3)
    body("This document describes the server-enforced rules of the NDFEX "
         "trading competition, the consequences of breaching them, and how "
         "breaches are displayed on the live scoreboard.")
    spacer(0.2)
    hr()

    # ── 1. Overview ───────────────────────────────────────────────────
    section("1. Overview")
    body("Teams connect to the NDFEX matching engine and trade 13 symbols "
         "over a multi-day competition. A live scoreboard tracks each "
         "team's aggregate PnL, per-symbol positions, and rule violations "
         "in real time.")
    body("Two rules are enforced server-side. Breaching either rule "
         "triggers an automatic, permanent penalty applied to your score. "
         "The exchange does <b>not</b> reject orders that would cause a "
         "breach \u2014 it is each team's responsibility to manage risk.")

    # ── 1b. Scoring ───────────────────────────────────────────────────
    section("2. Scoring")
    body("The competition runs over <b>two trading days</b>. Each day's "
         "PnL is calculated independently and combined into a final score "
         "using a weighted formula:")
    spacer(0.1)
    formula("Final Score = 0.25 \u00d7 max(0, Day 1 PnL) + 1.00 \u00d7 Day 2 PnL")
    spacer(0.1)
    body("Day 1 PnL counts at <b>25%</b> of its value, but is "
         "<b>clamped to a minimum of 0</b> \u2014 a negative Day 1 "
         "cannot hurt your final score. Day 2 PnL counts at "
         "<b>full value</b> with no floor. This means Day 1 is "
         "effectively a risk-free practice round: you can only "
         "benefit from it, while Day 2 performance dominates the "
         "final ranking.")
    body("Penalties from rule breaches (described below) are applied to "
         "each day's PnL <b>before</b> the weighting is applied.")
    spacer(0.1)
    subsection("Minimum Volume Requirement")
    body("To be eligible for the prize, a team must trade a minimum of "
         "<b>2,000 lots</b> (total volume across all symbols). Teams that "
         "do not reach this threshold are excluded from the final ranking, "
         "regardless of PnL.")
    body("The scoreboard displays a <b>VOL</b> tag next to each team name. "
         "The tag appears gray until the team reaches 2,000 lots, at which "
         "point it turns green.")

    hr()

    # ── 3. Rule 1: Position Limit ─────────────────────────────────────
    section("3. Rule 1 \u2014 Position Limit")
    subsection("Limit")
    body("The absolute position in any single symbol must not exceed "
         "<b>10</b>. That is, for each symbol:")
    formula("|position| &le; 10")
    body("Positions are tracked per-symbol. Long and short positions are "
         "both subject to this limit.")

    subsection("Penalty")
    body("When a team's absolute position in a symbol exceeds 10, that "
         "symbol's PnL contribution is <b>clamped so you keep losses "
         "but cannot profit</b>:")
    formula("penalized_pnl = min(0, symbol_pnl)")
    body("If the symbol PnL is positive, it becomes 0. If it is "
         "negative, the loss is kept as-is.")
    spacer(0.1)
    body("Key details:")
    bullet("The penalty applies <b>per-symbol</b> \u2014 breaching in GOLD "
           "does not affect your BLUE PnL.")
    bullet("The penalty is <b>permanent</b> once triggered. Even if you "
           "reduce your position back below 10, the clamp remains for "
           "the rest of the session.")
    bullet("If the symbol PnL is negative, you still keep the loss. "
           "The clamp only prevents you from profiting.")

    hr()

    # ── 4. Rule 2: Minimum PnL ────────────────────────────────────────
    section("4. Rule 2 \u2014 Minimum PnL")
    subsection("Limit")
    body("A team's aggregate PnL must not drop below <b>-5,000</b>:")
    formula("aggregate_pnl &ge; -5,000")

    subsection("Penalty")
    body("When a team's aggregate PnL drops below -5,000:")
    bullet("PnL is <b>frozen</b> at the breach value (the exact PnL at "
           "the moment of breach).")
    bullet("The team's session is <b>effectively over</b> \u2014 further "
           "trading will not improve or worsen the score.")
    bullet("The penalty is <b>permanent</b> for the remainder of the "
           "trading session.")

    hr()

    # ── 5. Scoreboard Indicators ──────────────────────────────────────
    section("5. Scoreboard Indicators")
    body("The live scoreboard uses visual indicators to flag rule breaches "
         "and volume eligibility:")
    spacer(0.1)

    indicator_data = [
        [th("Indicator"), th("Location"), th("Meaning")],
        [tc("POS (orange tag)"), tc("Leaderboard, next to team name"),
         tc("Team has breached the position limit in at least one symbol")],
        [tc("PNL (red tag)"), tc("Leaderboard, next to team name"),
         tc("Team has breached the minimum PnL rule")],
        [tc("VOL (gray tag)"), tc("Leaderboard, next to team name"),
         tc("Team has not yet reached the 2,000-lot minimum volume")],
        [tc("VOL (green tag)"), tc("Leaderboard, next to team name"),
         tc("Team has met the minimum volume requirement and is prize-eligible")],
        [tc("Orange cell"), tc("Position matrix"),
         tc("The specific symbol where the position limit was breached")],
    ]
    t = Table(indicator_data, colWidths=[1.5 * inch, 2.0 * inch, 3.2 * inch])
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), HexColor("#1a1a2e")),
        ("TEXTCOLOR", (0, 0), (-1, 0), white),
        ("FONTNAME", (0, 0), (-1, 0), "Helvetica-Bold"),
        ("FONTSIZE", (0, 0), (-1, -1), 9),
        ("TOPPADDING", (0, 0), (-1, -1), 5),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
        ("LEFTPADDING", (0, 0), (-1, -1), 6),
        ("GRID", (0, 0), (-1, -1), 0.5, HexColor("#cccccc")),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [white, HexColor("#f9f9f9")]),
        # Orange background for POS row
        ("BACKGROUND", (0, 1), (0, 1), HexColor("#fff3e0")),
        # Red background for PNL row
        ("BACKGROUND", (0, 2), (0, 2), HexColor("#ffebee")),
        # Gray background for VOL (not met) row
        ("BACKGROUND", (0, 3), (0, 3), HexColor("#eeeeee")),
        # Green background for VOL (met) row
        ("BACKGROUND", (0, 4), (0, 4), HexColor("#e8f5e9")),
        # Orange background for cell row
        ("BACKGROUND", (0, 5), (0, 5), HexColor("#fff3e0")),
        ("VALIGN", (0, 0), (-1, -1), "TOP"),
    ]))
    story.append(t)
    spacer(0.1)
    note("Indicators appear in real time as the scoreboard refreshes. "
         "All teams can see which teams have been penalized and which "
         "have met the volume requirement.")

    hr()

    # ── 6. Exchange Restarts ──────────────────────────────────────────
    section("6. Exchange Restarts")
    body("If the exchange crashes or needs to be restarted during a "
         "trading session:")
    spacer(0.1)
    bullet("Your current <b>PnL is saved</b> at the time of the crash.")
    bullet("All open positions are <b>marked to the last traded price</b> "
           "for each symbol (i.e., the PnL impact of those positions "
           "is crystallized).")
    bullet("All positions are <b>reset to 0</b>.")
    bullet("The exchange restarts with a <b>fresh order book</b>.")
    bullet("Teams keep their <b>accumulated PnL</b> from before the "
           "restart and continue trading from a flat position.")
    spacer(0.1)
    body("In short: a restart is like a forced liquidation at last price. "
         "You keep everything you earned (or lost), but start trading "
         "again from zero positions.")
    note("Rule breach flags (POS, PNL) persist across restarts. If you "
         "breached a rule before the restart, the penalty still applies.")

    hr()

    # ── 7. Disputes ───────────────────────────────────────────────────
    section("7. Disputes")
    body("The exchange log file is the <b>final source of truth</b> for "
         "all disputes, including disagreements over positions, PnL, "
         "fill prices, or rule breaches. Every order, fill, and cancel "
         "is recorded in the log with timestamps.")
    body("If a team believes the scoreboard is showing incorrect data, "
         "they may raise a dispute with the instructors. The exchange "
         "log will be consulted to resolve the issue, and its record "
         "is authoritative and final.")

    hr()

    # ── 8. Symbol Reference ───────────────────────────────────────────
    section("8. Symbol Reference")
    body("The following 13 symbols are available for trading:")
    spacer(0.1)

    sym_data = [
        [th("ID"), th("Ticker"), th("Name"), th("Tick Size"), th("Category")],
        [tc("1"), tc("GOLD"), tc("Gold"), tc("10"), tc("Commodity")],
        [tc("2"), tc("BLUE"), tc("Blue"), tc("5"), tc("Commodity")],
        [tc("3"), tc("KNAN"), tc("Keenan Hall"), tc("5"), tc("Dorm (ETF underlying)")],
        [tc("4"), tc("STED"), tc("St. Edward's Hall"), tc("5"), tc("Dorm (ETF underlying)")],
        [tc("5"), tc("FISH"), tc("Fisher Hall"), tc("5"), tc("Dorm (ETF underlying)")],
        [tc("6"), tc("DILN"), tc("Dillon Hall"), tc("5"), tc("Dorm (ETF underlying)")],
        [tc("7"), tc("SORN"), tc("Sorin Hall"), tc("5"), tc("Dorm (ETF underlying)")],
        [tc("8"), tc("RYAN"), tc("Ryan Hall"), tc("5"), tc("Dorm (ETF underlying)")],
        [tc("9"), tc("LYON"), tc("Lyons Hall"), tc("5"), tc("Dorm (ETF underlying)")],
        [tc("10"), tc("WLSH"), tc("Walsh Hall"), tc("5"), tc("Dorm (ETF underlying)")],
        [tc("11"), tc("LEWI"), tc("Lewis Hall"), tc("5"), tc("Dorm (ETF underlying)")],
        [tc("12"), tc("BDIN"), tc("Badin Hall"), tc("5"), tc("Dorm (ETF underlying)")],
        [tc("13"), tc("UNDY"), tc("Notre Dame Dorm ETF"), tc("10"), tc("ETF")],
    ]
    t = Table(sym_data, colWidths=[0.5 * inch, 0.7 * inch, 2.0 * inch, 0.8 * inch, 2.0 * inch])
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), HexColor("#1a1a2e")),
        ("TEXTCOLOR", (0, 0), (-1, 0), white),
        ("FONTNAME", (0, 0), (-1, 0), "Helvetica-Bold"),
        ("FONTSIZE", (0, 0), (-1, -1), 9),
        ("TOPPADDING", (0, 0), (-1, -1), 4),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 4),
        ("LEFTPADDING", (0, 0), (-1, -1), 6),
        ("GRID", (0, 0), (-1, -1), 0.5, HexColor("#cccccc")),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [white, HexColor("#f9f9f9")]),
        # Highlight UNDY row
        ("BACKGROUND", (0, -1), (-1, -1), HexColor("#fff3e0")),
    ]))
    story.append(t)
    spacer(0.1)
    note("GOLD and BLUE are standalone symbols not included in the UNDY ETF basket. "
         "The position limit of 10 applies to all 13 symbols equally.")

    hr()

    # ── 9. Tips / Best Practices ──────────────────────────────────────
    section("9. Tips &amp; Best Practices")
    bullet("<b>Monitor your positions constantly.</b> The position limit "
           "of 10 is small \u2014 a few rapid fills can push you over.")
    bullet("<b>Stay well within limits.</b> Aim to keep positions at 8 "
           "or below to give yourself a buffer against unexpected fills.")
    bullet("<b>Use the scoreboard.</b> Check the position matrix to see "
           "your live positions across all symbols at a glance.")
    bullet("<b>Watch your PnL.</b> If you're approaching -5,000, reduce "
           "risk immediately. Once breached, your session is over.")
    bullet("<b>Day 2 matters most.</b> With Day 1 weighted at only 25%, "
           "use it to learn the market and test strategies. Save "
           "aggressive plays for Day 2.")
    bullet("<b>Plan for restarts.</b> The exchange may restart during "
           "trading. Your bot should handle disconnections gracefully "
           "and be ready to reconnect and resume.")

    # Build
    doc.build(story)
    print(f"PDF written to {path}")


if __name__ == "__main__":
    build_pdf("/home/matt/NDFEX/docs/competition_rules.pdf")
