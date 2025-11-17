# text_processor.cpp

Path: `src/content/text_processor.cpp`

Purpose: Tokenization, normalization and simple text transformations used by content indexing and search.

Public functions / symbols:
- ``
- `for (size_t i = 0; i < chunk_start_idx; i++) {`
- `if (current_pos <= chunk_start_idx) {`
- `while (iss >> token) {`
- `for (int seed = 0; seed < 3; seed++) {`
- `for (int dim_offset = 0; dim_offset < 10; dim_offset++) {`
- `for (float val : embedding) {`
- `for (float& val : embedding) {`
- `if (auto pos = lang.find("text/x-"); pos != std::string::npos) {`
- `std::vector<float> embedding(EMBEDDING_DIM, 0.0f);`
- `std::istringstream iss(chunk_data);`
- `std::regex multi_space("  +");`

