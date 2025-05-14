#ifndef VYN_SOURCE_LOCATION_HPP
#define VYN_SOURCE_LOCATION_HPP

#include <string> // Required for std::string
#include <sstream> // Required for std::ostringstream

namespace vyn {
    struct SourceLocation {
        std::string filePath; // Added filePath
        unsigned int line;
        unsigned int column;

        // Default constructor
        SourceLocation() : filePath(""), line(0), column(0) {}

        // Constructor to initialize all members
        SourceLocation(const std::string& fp, unsigned int l, unsigned int c)
            : filePath(fp), line(l), column(c) {}

        // Method to convert SourceLocation to string
        std::string toString() const {
            std::ostringstream oss;
            oss << filePath << ":" << line << ":" << column;
            return oss.str();
        }
    };
} // namespace vyn

#endif // VYN_SOURCE_LOCATION_HPP
