#include "xtensor/xarray.hpp"
#include "xtensor-io/xio.hpp"
#include <vector>
#include <string>
#include <sstream>
#include <iostream>

xt::xarray<double> load_csv_by_column_names(const std::string& filename, const std::vector<std::string>& column_names, char delimiter = ',') {
    // 读取CSV文件的头部
    std::ifstream file(filename);
    std::string header_line;
    if (!std::getline(file, header_line)) {
        throw std::runtime_error("Failed to read the header line");
    }

    // 解析头部以获取列名和它们的索引
    std::istringstream header_stream(header_line);
    std::vector<std::string> headers;
    std::string header_token;
    while (std::getline(header_stream, header_token, delimiter)) {
        headers.push_back(header_token);
    }

    // 找到所需列名的索引
    std::vector<std::vector<size_t>> selected_columns;
    for (const auto& name : column_names) {
        auto it = std::find(headers.begin(), headers.end(), name);
        if (it != headers.end()) {
            size_t index = std::distance(headers.begin(), it);
            selected_columns.push_back({index, index + 1});
        } else {
            throw std::runtime_error("Column not found: " + name);
        }
    }

    // 加载选定的列
    return xt::load_csv<double>(filename, delimiter, 0, selected_columns);
}