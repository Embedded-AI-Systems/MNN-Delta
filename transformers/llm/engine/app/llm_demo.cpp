//
//  llm_demo.cpp
//
//  Created by MNN on 2023/03/24.
//  ZhaodeWang
//

#include "llm/llm.hpp"
#define MNN_OPEN_TIME_TRACE
#include <MNN/AutoTime.hpp>
#include <MNN/expr/ExecutorScope.hpp>
#include <fstream>
#include <sstream>
#include <stdlib.h>
using namespace MNN::Transformer;
static void trace_prepare(Llm* llm) {
    MNN_PRINT("Prepare for resize opt Begin\n");
    llm->trace(true);
    std::ostringstream cacheOs;
    llm->response("Hello", &cacheOs);
    MNN_PRINT("Prepare for resize opt End\n");
    llm->trace(false);
    llm->reset();
}

std::vector<std::vector<std::string>> parse_csv(const std::vector<std::string>& lines) {
    std::vector<std::vector<std::string>> csv_data;
    std::string line;
    std::vector<std::string> row;
    std::string cell;
    bool insideQuotes = false;
    bool startCollecting = false;

    // content to stream
    std::string content = "";
    for (auto line : lines) {
        content = content + line + "\n";
    }
    std::istringstream stream(content);

    while (stream.peek() != EOF) {
        char c = stream.get();
        if (c == '"') {
            if (insideQuotes && stream.peek() == '"') { // quote
                cell += '"';
                stream.get(); // skip quote
            } else {
                insideQuotes = !insideQuotes; // start or end text in quote
            }
            startCollecting = true;
        } else if (c == ',' && !insideQuotes) { // end element, start new element
            row.push_back(cell);
            cell.clear();
            startCollecting = false;
        } else if ((c == '\n' || stream.peek() == EOF) && !insideQuotes) { // end line
            row.push_back(cell);
            csv_data.push_back(row);
            cell.clear();
            row.clear();
            startCollecting = false;
        } else {
            cell += c;
            startCollecting = true;
        }
    }
    return csv_data;
}

static int benchmark(Llm* llm, const std::vector<std::string>& prompts) {
    for (int i = 0; i < prompts.size(); i++) {
        const auto& prompt = prompts[i];
        // prompt start with '#' will be ignored
        if (prompt.substr(0, 1) == "#") {
            continue;
        }
        llm->response(prompt);
    }
    printf("\n#################################\n");
    printf("prompt tokens num  = %d\n", llm->getTotalPromptLen());
    printf("decode tokens num  = %d\n", llm->getTotalDecodeLen());
    printf("prefill time = %.2f s\n", llm->getTotalPrefillTime());
    printf(" decode time = %.2f s\n", llm->getTotalDecodeTime());
    printf("prefill speed = %.2f tok/s\n", llm->average_prefill_speed());
    printf(" decode speed = %.2f tok/s\n", llm->average_decode_speed());
    printf("##################################\n");
    return 0;
}

static int ceval(Llm* llm, const std::vector<std::string>& lines, std::string filename) {
    auto csv_data = parse_csv(lines);
    int right = 0, wrong = 0;
    std::vector<std::string> answers;
    for (int i = 1; i < csv_data.size(); i++) {
        const auto& elements = csv_data[i];
        std::string prompt = elements[1];
        prompt += "\n\nA. " + elements[2];
        prompt += "\nB. " + elements[3];
        prompt += "\nC. " + elements[4];
        prompt += "\nD. " + elements[5];
        prompt += "\n\n";
        printf("%s", prompt.c_str());
        printf("## 进度: %d / %lu\n", i, lines.size() - 1);
        auto res = llm->response(prompt.c_str());
        answers.push_back(res);
    }
    {
        auto position = filename.rfind("/");
        if (position != std::string::npos) {
            filename = filename.substr(position + 1, -1);
        }
        position = filename.find("_val");
        if (position != std::string::npos) {
            filename.replace(position, 4, "_res");
        }
        std::cout << "store to " << filename << std::endl;
    }
    std::ofstream ofp(filename);
    ofp << "id,answer" << std::endl;
    for (int i = 0; i < answers.size(); i++) {
        auto& answer = answers[i];
        ofp << i << ",\""<< answer << "\"" << std::endl;
    }
    ofp.close();
    return 0;
}

static int eval(Llm* llm, std::string prompt_file) {
    std::cout << "prompt file is " << prompt_file << std::endl;
    std::ifstream prompt_fs(prompt_file);
    std::vector<std::string> prompts;
    std::string prompt;
    while (std::getline(prompt_fs, prompt)) {
        if (prompt.back() == '\r') {
            prompt.pop_back();
        }
        prompts.push_back(prompt);
    }
    prompt_fs.close();
    if (prompts.empty()) {
        return 1;
    }
    // ceval
    if (prompts[0] == "id,question,A,B,C,D,answer") {
        return ceval(llm, prompts, prompt_file);
    }
    return benchmark(llm, prompts);
}

int main(int argc, const char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " config.json <prompt.txt>" << std::endl;
        return 0;
    }
    MNN::BackendConfig backendConfig;
    auto executor = MNN::Express::Executor::newExecutor(MNN_FORWARD_CPU, backendConfig, 1);
    MNN::Express::ExecutorScope s(executor);

    std::string config_path = argv[1];
    std::cout << "config path is " << config_path << std::endl;
    std::unique_ptr<Llm> llm(Llm::createLLM(config_path));
    llm->set_config("{\"tmp_path\":\"tmp\"}");
    {
        AUTOTIME;
        llm->load();
    }
    if (true) {
        AUTOTIME;
        trace_prepare(llm.get());
    }
    if (argc < 3) {
        llm->chat();
        return 0;
    }
    std::string prompt_file = argv[2];
    return eval(llm.get(), prompt_file);
}
