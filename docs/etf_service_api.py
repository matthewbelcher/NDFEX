"""Generate ETF Service REST API documentation as a PDF."""

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
        "Endpoint", fontName="Courier-Bold", fontSize=11,
        textColor=HexColor("#e17055"), spaceBefore=4, spaceAfter=4,
    ))
    styles.add(ParagraphStyle(
        "Note", parent=styles["Normal"], fontSize=9, leading=12,
        textColor=HexColor("#555555"), leftIndent=16, spaceAfter=6,
        fontName="Helvetica-Oblique",
    ))
    styles.add(ParagraphStyle(
        "TableCell", fontName="Helvetica", fontSize=9, leading=11,
    ))
    styles.add(ParagraphStyle(
        "TableCellMono", fontName="Courier", fontSize=9, leading=11,
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

    def code(text):
        story.append(Preformatted(text, styles["CodeBlock"]))

    def endpoint(method, path):
        story.append(Paragraph(f"{method} {path}", styles["Endpoint"]))

    def note(text):
        story.append(Paragraph(text, styles["Note"]))

    def spacer(h=0.15):
        story.append(Spacer(1, h * inch))

    def hr():
        story.append(HRFlowable(
            width="100%", thickness=0.5, color=HexColor("#cccccc"),
            spaceBefore=6, spaceAfter=6,
        ))

    # ── Title page ──────────────────────────────────────────────────
    spacer(1.0)
    title("NDFEX ETF Service")
    subtitle("REST API Documentation")
    spacer(0.3)
    body("The ETF Service provides a REST API for creating and redeeming "
         "shares of the <b>UNDY</b> (Notre Dame Dorm ETF). One UNDY share "
         "represents one share of each of the 10 underlying dorm symbols.")
    spacer(0.2)

    # Connection info
    data = [
        ["REST API", "http://129.74.160.245:5000"],
        ["WebSocket", "ws://129.74.160.245:9003"],
        ["Authentication", "HTTP Basic Auth (team credentials)"],
    ]
    t = Table(data, colWidths=[1.8 * inch, 4.0 * inch])
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (0, -1), HexColor("#f0f0f0")),
        ("FONTNAME", (0, 0), (0, -1), "Helvetica-Bold"),
        ("FONTNAME", (1, 0), (1, -1), "Courier"),
        ("FONTSIZE", (0, 0), (-1, -1), 10),
        ("TOPPADDING", (0, 0), (-1, -1), 5),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
        ("LEFTPADDING", (0, 0), (-1, -1), 8),
        ("GRID", (0, 0), (-1, -1), 0.5, HexColor("#cccccc")),
    ]))
    story.append(t)

    spacer(0.3)
    hr()

    # ── ETF Composition ─────────────────────────────────────────────
    section("ETF Composition")
    body("1 UNDY = 1 share of each underlying dorm symbol:")
    spacer(0.1)

    sym_data = [
        ["Symbol ID", "Ticker", "Name", "Tick Size"],
        ["3", "KNAN", "Keenan Hall", "5"],
        ["4", "STED", "St. Edward's Hall", "5"],
        ["5", "FISH", "Fisher Hall", "5"],
        ["6", "DILN", "Dillon Hall", "5"],
        ["7", "SORN", "Sorin Hall", "5"],
        ["8", "RYAN", "Ryan Hall", "5"],
        ["9", "LYON", "Lyons Hall", "5"],
        ["10", "WLSH", "Walsh Hall", "5"],
        ["11", "LEWI", "Lewis Hall", "5"],
        ["12", "BDIN", "Badin Hall", "5"],
        ["13", "UNDY", "Notre Dame Dorm ETF", "10"],
    ]
    t = Table(sym_data, colWidths=[1.0 * inch, 0.8 * inch, 2.5 * inch, 0.8 * inch])
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
        ("BACKGROUND", (0, -1), (-1, -1), HexColor("#fff3e0")),
    ]))
    story.append(t)
    spacer(0.1)
    note("Additional trading symbols: GOLD (ID 1, tick 10) and BLUE (ID 2, tick 5) "
         "are not part of the ETF basket.")

    # ── Authentication ───────────────────────────────────────────────
    section("Authentication")
    body("State-mutating endpoints (<b>/create</b>, <b>/redeem</b>) and the "
         "<b>/whoami</b> endpoint require HTTP Basic Auth. Credentials are the "
         "same team name and password issued for the matching engine "
         "(distributed to your team — see your team lead). The authenticated "
         "user is bound to one client_id; you may only create or redeem on "
         "your own behalf.")
    spacer(0.1)

    body("Read-only endpoints (<b>/health</b>, <b>/symbols</b>, "
         "<b>/positions/&lt;id&gt;</b>, <b>/history</b>) remain public so "
         "the dashboard and other observers can monitor the market without "
         "credentials.")
    spacer(0.1)

    body("<b>How to authenticate:</b>")
    body("• <b>curl:</b> pass <code>-u &lt;team_name&gt;:&lt;password&gt;</code> "
         "on every request.")
    body("• <b>Python requests:</b> pass <code>auth=(team_name, password)</code>.")
    body("• <b>Browser (web UI):</b> the page prompts for credentials on first "
         "load; the browser remembers them for the session.")
    spacer(0.1)

    body("<b>Auth error responses:</b>")
    auth_err_data = [
        ["Status", "Cause"],
        ["401 Unauthorized", "Missing or invalid credentials"],
        ["403 Forbidden", "Body client_id does not match authenticated user"],
    ]
    t = Table(auth_err_data, colWidths=[2.0 * inch, 4.7 * inch])
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), HexColor("#1a1a2e")),
        ("TEXTCOLOR", (0, 0), (-1, 0), white),
        ("FONTNAME", (0, 0), (-1, 0), "Helvetica-Bold"),
        ("FONTNAME", (0, 1), (0, -1), "Courier"),
        ("FONTSIZE", (0, 0), (-1, -1), 9),
        ("TOPPADDING", (0, 0), (-1, -1), 5),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
        ("LEFTPADDING", (0, 0), (-1, -1), 6),
        ("GRID", (0, 0), (-1, -1), 0.5, HexColor("#cccccc")),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [white, HexColor("#f9f9f9")]),
    ]))
    story.append(t)

    hr()

    # ── Endpoints ────────────────────────────────────────────────────
    section("API Endpoints")

    # --- GET /health ---
    subsection("Health Check")
    endpoint("GET", "/health")
    body("Returns service health status. Use for monitoring. <b>No auth required.</b>")
    code('{\n  "status": "ok",\n  "service": "etf_service"\n}')

    hr()

    # --- GET /symbols ---
    subsection("List Symbols")
    endpoint("GET", "/symbols")
    body("Returns all symbol definitions, the ETF symbol ID, and the list of "
         "underlying symbol IDs.")
    code('{\n'
         '  "symbols": [\n'
         '    {"id": 1, "ticker": "GOLD", "name": "Gold", "tick_size": 10},\n'
         '    {"id": 2, "ticker": "BLUE", "name": "Blue", "tick_size": 5},\n'
         '    ...\n'
         '  ],\n'
         '  "etf_symbol": 13,\n'
         '  "underlying_symbols": [3, 4, 5, 6, 7, 8, 9, 10, 11, 12]\n'
         '}')

    hr()

    # --- GET /positions/<client_id> ---
    subsection("Get Client Positions")
    endpoint("GET", "/positions/&lt;client_id&gt;")
    body("Returns all positions for the given client. Positions combine "
         "clearing fills with ETF create/redeem adjustments. Only non-zero "
         "positions are included.")
    code('GET /positions/1\n\n'
         '{\n'
         '  "client_id": 1,\n'
         '  "positions": {\n'
         '    "GOLD": 50,\n'
         '    "KNAN": 10,\n'
         '    "UNDY": 5\n'
         '  }\n'
         '}')

    hr()

    # --- GET /positions/<client_id>/<symbol> ---
    subsection("Get Single Position")
    endpoint("GET", "/positions/&lt;client_id&gt;/&lt;symbol&gt;")
    body("Returns the position for a specific client and symbol ID.")
    code('GET /positions/1/13\n\n'
         '{\n'
         '  "client_id": 1,\n'
         '  "symbol": 13,\n'
         '  "ticker": "UNDY",\n'
         '  "position": 5\n'
         '}')

    hr()

    # --- GET /whoami ---
    subsection("Identify Authenticated User")
    endpoint("GET", "/whoami")
    body("<b>Requires auth.</b> Returns the client_id and team name bound to "
         "the supplied credentials. Useful for sanity-checking your login.")
    code('GET /whoami\n\n'
         '{\n'
         '  "client_id": 1,\n'
         '  "name": "team1"\n'
         '}')

    hr()

    # --- POST /create ---
    subsection("Create ETF Shares")
    endpoint("POST", "/create")
    body("<b>Requires auth.</b> Exchange underlying dorm positions for UNDY "
         "ETF shares for the authenticated client. The client must hold at "
         "least <b>amount</b> shares of <i>each</i> of the 10 underlying "
         "symbols. The operation is atomic: all underlying positions are "
         "debited and UNDY is credited in one step.")
    spacer(0.1)

    body("<b>Request:</b> the client_id is taken from your authenticated "
         "credentials. You may optionally include <code>client_id</code> in "
         "the body, but it must match — otherwise the request is rejected "
         "with 403.")
    code('POST /create\n'
         'Authorization: Basic <base64(team_name:password)>\n'
         'Content-Type: application/json\n\n'
         '{\n  "amount": 10\n}')

    body("<b>Success Response (200):</b>")
    code('{\n'
         '  "success": true,\n'
         '  "message": "Created 10 UNDY from underlying positions",\n'
         '  "undy_balance": 15\n'
         '}')

    body("<b>Error Response (400):</b>")
    code('{\n'
         '  "success": false,\n'
         '  "message": "Insufficient positions: KNAN: have 5, need 10",\n'
         '  "undy_balance": 5\n'
         '}')

    hr()

    # --- POST /redeem ---
    subsection("Redeem ETF Shares")
    endpoint("POST", "/redeem")
    body("<b>Requires auth.</b> Exchange UNDY ETF shares back into underlying "
         "dorm positions for the authenticated client. The client must hold "
         "at least <b>amount</b> UNDY shares. Each underlying symbol is "
         "credited with <b>amount</b> shares.")
    spacer(0.1)

    body("<b>Request:</b>")
    code('POST /redeem\n'
         'Authorization: Basic <base64(team_name:password)>\n'
         'Content-Type: application/json\n\n'
         '{\n  "amount": 5\n}')

    body("<b>Success Response (200):</b>")
    code('{\n'
         '  "success": true,\n'
         '  "message": "Redeemed 5 UNDY to underlying positions",\n'
         '  "undy_balance": 10\n'
         '}')

    body("<b>Error Response (400):</b>")
    code('{\n'
         '  "success": false,\n'
         '  "message": "Insufficient UNDY: have 5, need 10",\n'
         '  "undy_balance": 5\n'
         '}')

    hr()

    # --- GET /history ---
    subsection("Transaction History")
    endpoint("GET", "/history")
    body("Returns the full create/redeem history for auditing.")
    code('{\n'
         '  "history": [\n'
         '    {"type": "create", "client_id": 1, "amount": 10},\n'
         '    {"type": "redeem", "client_id": 1, "amount": 5}\n'
         '  ]\n'
         '}')

    hr()

    # ── Error Reference ──────────────────────────────────────────────
    section("Error Reference")
    body("Error responses use this JSON shape (the <b>undy_balance</b> field "
         "is omitted on auth failures):")
    code('{\n'
         '  "success": false,\n'
         '  "message": "<error details>",\n'
         '  "undy_balance": <current UNDY balance>\n'
         '}')
    spacer(0.1)

    err_data = [
        ["Status / Error", "Cause"],
        ["401 Authentication required", "No Authorization header on a protected endpoint"],
        ["401 Invalid credentials", "Wrong team name or password"],
        ["403 client_id does not match", "Body client_id is not your authenticated client_id"],
        ["400 Missing amount", "amount field absent from JSON body"],
        ["400 Invalid amount", "amount not convertible to integer"],
        ["400 Invalid client_id", "client_id field present but not an integer"],
        ["400 Amount must be positive", "amount <= 0"],
        ["400 Insufficient positions: ...", "Not enough underlying shares to create"],
        ["400 Insufficient UNDY: ...", "Not enough UNDY shares to redeem"],
    ]
    t = Table(err_data, colWidths=[2.5 * inch, 4.2 * inch])
    t.setStyle(TableStyle([
        ("BACKGROUND", (0, 0), (-1, 0), HexColor("#1a1a2e")),
        ("TEXTCOLOR", (0, 0), (-1, 0), white),
        ("FONTNAME", (0, 0), (-1, 0), "Helvetica-Bold"),
        ("FONTNAME", (0, 1), (0, -1), "Courier"),
        ("FONTSIZE", (0, 0), (-1, -1), 9),
        ("TOPPADDING", (0, 0), (-1, -1), 5),
        ("BOTTOMPADDING", (0, 0), (-1, -1), 5),
        ("LEFTPADDING", (0, 0), (-1, -1), 6),
        ("GRID", (0, 0), (-1, -1), 0.5, HexColor("#cccccc")),
        ("ROWBACKGROUNDS", (0, 1), (-1, -1), [white, HexColor("#f9f9f9")]),
    ]))
    story.append(t)

    # ── Usage Examples ───────────────────────────────────────────────
    section("Usage Examples (curl)")

    body("Substitute your team name and password (issued separately) for "
         "<code>team1:PASSWORD</code> in the examples below.")
    spacer(0.1)

    body("<b>Check service health (no auth):</b>")
    code("curl http://129.74.160.245:5000/health")
    spacer(0.1)

    body("<b>Get positions for client 1 (no auth):</b>")
    code("curl http://129.74.160.245:5000/positions/1")
    spacer(0.1)

    body("<b>Confirm your login:</b>")
    code('curl -u team1:PASSWORD http://129.74.160.245:5000/whoami')
    spacer(0.1)

    body("<b>Create 10 UNDY shares (auth required):</b>")
    code('curl -u team1:PASSWORD -X POST http://129.74.160.245:5000/create \\\n'
         '  -H "Content-Type: application/json" \\\n'
         '  -d \'{"amount": 10}\'')
    spacer(0.1)

    body("<b>Redeem 5 UNDY shares (auth required):</b>")
    code('curl -u team1:PASSWORD -X POST http://129.74.160.245:5000/redeem \\\n'
         '  -H "Content-Type: application/json" \\\n'
         '  -d \'{"amount": 5}\'')
    spacer(0.1)

    body("<b>Python example:</b>")
    code('import requests\n\n'
         'BASE = "http://129.74.160.245:5000"\n'
         'AUTH = ("team1", "PASSWORD")  # your team credentials\n\n'
         '# Check positions (no auth needed)\n'
         'r = requests.get(f"{BASE}/positions/1")\n'
         'print(r.json())\n\n'
         '# Confirm login\n'
         'r = requests.get(f"{BASE}/whoami", auth=AUTH)\n'
         'print(r.json())  # {"client_id": 1, "name": "team1"}\n\n'
         '# Create ETF shares for the authenticated client\n'
         'r = requests.post(f"{BASE}/create", auth=AUTH,\n'
         '    json={"amount": 10})\n'
         'print(r.json())')

    # ── Position Calculation ─────────────────────────────────────────
    section("Position Calculation")
    body("The effective position for any client/symbol pair is:")
    code("effective_position = clearing_position + etf_adjustment")
    body("<b>clearing_position</b> comes from fill messages on the clearing "
         "multicast feed (trades executed on the matching engine).")
    body("<b>etf_adjustment</b> comes from create/redeem operations through "
         "this API. Creating UNDY debits underlyings and credits UNDY; "
         "redeeming does the reverse.")
    note("The ETF service maintains its own position ledger. Create/redeem "
         "adjustments are tracked locally and do not generate fills on the "
         "matching engine.")

    # Build
    doc.build(story)
    print(f"PDF written to {path}")


if __name__ == "__main__":
    build_pdf("/home/matt/NDFEX/docs/etf_service_api.pdf")
