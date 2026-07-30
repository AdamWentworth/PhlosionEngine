// Event.h

#pragma once
#include <string>
#include <cstdint>

// Define an enum for event types. Expand this as needed.
enum class EventType {
    KeyDown,
    KeyUp,
    MouseMoved,
    MouseButtonDown,
    MouseButtonUp,
    RoundPhaseChanged
};

// Base event class.
class Event {
public:
    Event(EventType type) : type(type) {}
    virtual ~Event() = default;
    EventType getType() const { return type; }
    virtual std::string toString() const { return "Generic Event"; }
private:
    EventType type;
};

// --- New Specialized Event Classes ---

class MouseButtonDownEvent : public Event {
public:
    // button matches SDL: 1=left, 2=middle, 3=right
    MouseButtonDownEvent(int x, int y, std::uint8_t button = 0)
        : Event(EventType::MouseButtonDown), x(x), y(y), button(button) {}

    int getX() const { return x; }
    int getY() const { return y; }
    std::uint8_t getButton() const { return button; }

    std::string toString() const override {
        return "MouseButtonDownEvent at (" + std::to_string(x) + ", " + std::to_string(y) +
               "), button=" + std::to_string((int)button);
    }
private:
    int x, y;
    std::uint8_t button;
};

class MouseButtonUpEvent : public Event {
public:
    // button matches SDL: 1=left, 2=middle, 3=right
    MouseButtonUpEvent(int x, int y, std::uint8_t button = 0)
        : Event(EventType::MouseButtonUp), x(x), y(y), button(button) {}

    int getX() const { return x; }
    int getY() const { return y; }
    std::uint8_t getButton() const { return button; }

    std::string toString() const override {
        return "MouseButtonUpEvent at (" + std::to_string(x) + ", " + std::to_string(y) +
               "), button=" + std::to_string((int)button);
    }
private:
    int x, y;
    std::uint8_t button;
};

class MouseMotionEvent : public Event {
public:
    MouseMotionEvent(int x, int y)
        : Event(EventType::MouseMoved), x(x), y(y) {}
    int getX() const { return x; }
    int getY() const { return y; }
    std::string toString() const override {
        return "MouseMotionEvent at (" + std::to_string(x) + ", " + std::to_string(y) + ")";
    }
private:
    int x, y;
};
