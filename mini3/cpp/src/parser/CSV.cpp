#include "parser/CSV.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <limits>
#include <algorithm>

size_t CSV::size() const {
    return rows.size();
}

double parseDouble(const std::string &field) {
    if (field.empty() || field == "NULL" || field == "Unspecified")
        return std::numeric_limits<double>::quiet_NaN();
    try {
        return std::stod(field);
    } catch (const std::exception&) {
        return std::numeric_limits<double>::quiet_NaN();
    }
}

int parseInt(const std::string &field) {
    if (field.empty() || field == "NULL" || field == "Unspecified")
        return std::numeric_limits<int>::min();
    try {
        return std::stoi(field);
    } catch (const std::exception&) {
        return std::numeric_limits<int>::min();
    }
}

std::string parseString(const std::string &field) {
    return field.empty() || field == "NULL" || field == "Unspecified" ? "" : field;
}

std::string stripQuotes(const std::string &field) {
    if (field.size() >= 2 && field.front() == '"' && field.back() == '"') {
        return field.substr(1, field.size() - 2);
    }
    return field;
}

std::vector<std::string> splitCSVLine(const std::string &line) {
    std::vector<std::string> fields;
    std::string field;
    bool inQuotes = false;
    for (char c : line) {
        if (c == '"') {
            inQuotes = !inQuotes;
        } else if (c == ',' && !inQuotes) {
            fields.push_back(stripQuotes(field));
            field.clear();
        } else {
            field += c;
        }
    }
    fields.push_back(stripQuotes(field));
    return fields;
}

CSV makeCSV(const std::string &filename) {
    int ignoredRows = 0;
    CSV data;
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        return data;
    }

    std::string line;
    while (std::getline(file, line)) {
        auto fields = splitCSVLine(line);
        if (fields.size() < 29) {
            ignoredRows++;
            continue;
        }

        try {
            CSVRow row;
            row.crash_date = parseString(fields[0]);
            row.crash_time = parseString(fields[1]);
            row.borough = parseString(fields[2]);
            row.zip_code = parseInt(fields[3]);
            row.latitude = parseDouble(fields[4]);
            row.longitude = parseDouble(fields[5]);
            row.location = parseString(fields[6]);
            row.on_street_name = parseString(fields[7]);
            row.cross_street_name = parseString(fields[8]);
            row.off_street_name = parseString(fields[9]);
            row.persons_injured = parseInt(fields[10]);
            row.persons_killed = parseInt(fields[11]);
            row.pedestrians_injured = parseInt(fields[12]);
            row.pedestrians_killed = parseInt(fields[13]);
            row.cyclists_injured = parseInt(fields[14]);
            row.cyclists_killed = parseInt(fields[15]);
            row.motorists_injured = parseInt(fields[16]);
            row.motorists_killed = parseInt(fields[17]);
            row.contributing_factor_vehicle_1 = parseString(fields[18]);
            row.contributing_factor_vehicle_2 = parseString(fields[19]);
            row.contributing_factor_vehicle_3 = parseString(fields[20]);
            row.contributing_factor_vehicle_4 = parseString(fields[21]);
            row.contributing_factor_vehicle_5 = parseString(fields[22]);
            row.collision_id = parseInt(fields[23]);
            row.vehicle_type_code_1 = parseString(fields[24]);
            row.vehicle_type_code_2 = parseString(fields[25]);
            row.vehicle_type_code_3 = parseString(fields[26]);
            row.vehicle_type_code_4 = parseString(fields[27]);
            row.vehicle_type_code_5 = parseString(fields[28]);
            
            data.rows.push_back(row);
        } catch (const std::exception &e) {
            ignoredRows++;
        }
    }

    std::cout << "Ignored Rows = " << ignoredRows << std::endl;
    std::cout << "Number of rows successfully parsed: " << data.size() << std::endl;
    return data;
}