#ifndef CSV_H
#define CSV_H

#include <string>
#include <vector>

struct CSVRow {
    std::string crash_date;
    std::string crash_time;
    std::string borough;
    int zip_code;
    double latitude;
    double longitude;
    std::string location;
    std::string on_street_name;
    std::string cross_street_name;
    std::string off_street_name;
    int persons_injured;
    int persons_killed;
    int pedestrians_injured;
    int pedestrians_killed;
    int cyclists_injured;
    int cyclists_killed;
    int motorists_injured;
    int motorists_killed;
    std::string contributing_factor_vehicle_1;
    std::string contributing_factor_vehicle_2;
    std::string contributing_factor_vehicle_3;
    std::string contributing_factor_vehicle_4;
    std::string contributing_factor_vehicle_5;
    int collision_id;
    std::string vehicle_type_code_1;
    std::string vehicle_type_code_2;
    std::string vehicle_type_code_3;
    std::string vehicle_type_code_4;
    std::string vehicle_type_code_5;
};

class CSV {
public:
    std::vector<CSVRow> rows;
    size_t size() const;
};

CSV makeCSV(const std::string &filename);

double parseDouble(const std::string &field);
int parseInt(const std::string &field);
std::string parseString(const std::string &field);
std::string stripQuotes(const std::string &field);
std::vector<std::string> splitCSVLine(const std::string &line);

#endif 