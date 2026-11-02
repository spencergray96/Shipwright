#ifndef SOH_CONSOLE_H
#define SOH_CONSOLE_H

#include <ship/window/gui/ConsoleWindow.h>

class SohConsoleWindow : public Ship::ConsoleWindow {
  public:
    using ConsoleWindow::ConsoleWindow;

  protected:
    void DrawElement() override;
    // Our commands' hint trees (ConsoleSink::Hints, sturdy-bassoon#292); vanilla commands keep the usage line.
    bool CustomCommandHint(const std::string& input, std::string* hint) override;
};

#endif // SOH_CONSOLE_H
