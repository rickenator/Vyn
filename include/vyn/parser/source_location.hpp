#ifndef VYN_PARSER_SOURCE_LOCATION_HPP
#define VYN_PARSER_SOURCE_LOCATION_HPP

#include <string>
#include <utility> // For std::move

namespace vyn {

struct SourceLocation {
    std::string filePath;
    unsigned int line; // Changed to unsigned int
    unsigned int column; // Changed to unsigned int

    SourceLocation(std::string filePath = "", unsigned int line = 1, unsigned int column = 1) // Changed to unsigned int
        : filePath(std::move(filePath)), line(line), column(column) {}

    std::string toString() const {
        return this->filePath + ":" + std::to_string(this->line) + ":" + std::to_string(this->column);
    }
};

} // namespace vyn

#endif // VYN_PARSER_SOURCE_LOCATION_HPP
