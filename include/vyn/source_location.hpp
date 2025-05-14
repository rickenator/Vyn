#ifndef VYN_SOURCE_LOCATION_HPP
#define VYN_SOURCE_LOCATION_HPP

#include <string> // Required for std::string

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
    };
} // namespace vyn

#endif // VYN_SOURCE_LOCATION_HPP
