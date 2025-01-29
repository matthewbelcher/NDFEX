#include <bots/md_client.H>

#include <iostream>

#include <ftxui/dom/elements.hpp>

#include <ftxui/screen/screen.hpp>
#include <ftxui/screen/string.hpp>
#include <ftxui/screen/terminal.hpp>

#include <ftxui/component/component.hpp>
#include <ftxui/component/component_base.hpp>
#include <ftxui/component/screen_interactive.hpp>

#include <spdlog/async.h>
#include <spdlog/sinks/daily_file_sink.h>

using namespace ftxui;

int main(int argc, char* argv[]) {

    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << "<mcast ip address> <snapshot ip address> <mcast bind ip>" << std::endl;
        return 1;
    }

    std::string mcast_ip = argv[1];
    std::string snapshot_ip = argv[2];
    std::string mcast_bind_ip = argv[3];

    auto logger = spdlog::daily_logger_mt<spdlog::async_factory>("async_logger", "logs/viewer");

    // create market data client
    ndfex::bots::MDClient md_client(mcast_ip, 12345, snapshot_ip, 12345, mcast_bind_ip, logger);
    md_client.wait_for_snapshot();

    auto qty_at_prc = [&](uint32_t symbol, bool is_bid, ndfex::bots::MDClient& md_client) {
        auto levels = is_bid ? md_client.get_best_bid(symbol) : md_client.get_best_ask(symbol);
        if (levels.price == 0) {
            return window(text(L" Quantity at price "), text(L" No data ")) | color(Color::GrayDark);
        }

        auto content = vbox({
            hbox({text(std::to_string(levels.quantity)), text("@"),
                  text(std::to_string(levels.price)), (is_bid ? text(L"") : text("")) | bold})
                | (is_bid ? color(Color::Aquamarine1) : color(Color::RedLight)),
        });
        return content;
    };

    auto summary = [&] (ndfex::bots::MDClient& md_client) {
        if (md_client.get_best_bid(1).price == 0) {
            return window(text(L" Summary "), text(L" No data ")) | color(Color::GrayDark);
        }

        auto content = vbox({
            hbox({text(L"Symbol: "), text(std::to_string(1)) | bold}) | color(Color::Green),
            hbox(qty_at_prc(1, true, md_client), separator(), qty_at_prc(1, false, md_client)),
            hbox({text(L"Symbol: "), text(std::to_string(2)) | bold}) | color(Color::Green),
            hbox(qty_at_prc(2, true, md_client), separator(), qty_at_prc(2, false, md_client))
        });
        return window(text(L" Summary "), content);
    };


    std::string resetPosition;
    while (true) {
        md_client.process();

        auto document = vbox({
            summary(md_client),
        });

        auto screen = Screen::Create(Dimension::Full(), Dimension::Fit(document));

        Render(screen, document);

        std::cout << resetPosition;
        screen.Print(); // Print the screen.
        resetPosition = screen.ResetPosition();
    }


}