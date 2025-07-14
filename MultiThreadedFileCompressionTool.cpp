#include <iostream>
#include <fstream>
#include <unordered_map>
#include <queue>
#include <thread>
#include <mutex>
#include <vector>
#include <chrono>
using namespace std;
using namespace chrono;

// Huffman Tree Node
struct HuffmanNode {
    char character;
    int frequency;
    HuffmanNode *leftChild, *rightChild;
    HuffmanNode(char ch, int freq) : character(ch), frequency(freq), leftChild(nullptr), rightChild(nullptr) {}
};

// Node Comparison
struct HuffmanCompare {
    bool operator()(HuffmanNode* a, HuffmanNode* b) {
        return a->frequency > b->frequency;
    }
};

mutex frequencyMutex;

// Count frequency in chunks
void countFrequencyThread(const string& data, unordered_map<char, int>& globalMap, int begin, int finish) {
    unordered_map<char, int> localMap;
    for (int i = begin; i < finish; ++i) localMap[data[i]]++;
    lock_guard<mutex> guard(frequencyMutex);
    for (auto& pair : localMap) globalMap[pair.first] += pair.second;
}

// Count frequency single-threaded
void countFrequencySingle(const string& data, unordered_map<char, int>& freqMap) {
    for (char ch : data) freqMap[ch]++;
}

// Generate binary codes
void createHuffmanCodes(HuffmanNode* rootNode, string currCode, unordered_map<char, string>& codeMap) {
    if (!rootNode) return;
    if (!rootNode->leftChild && !rootNode->rightChild) codeMap[rootNode->character] = currCode;
    createHuffmanCodes(rootNode->leftChild, currCode + "0", codeMap);
    createHuffmanCodes(rootNode->rightChild, currCode + "1", codeMap);
}

// Delete Huffman Tree
void deleteHuffmanTree(HuffmanNode* node) {
    if (!node) return;
    deleteHuffmanTree(node->leftChild);
    deleteHuffmanTree(node->rightChild);
    delete node;
}

// Encode text
string huffmanEncode(const string& data, unordered_map<char, string>& codeTable) {
    string encoded;
    for (char ch : data) encoded += codeTable[ch];
    return encoded;
}

// Decode binary
string huffmanDecode(HuffmanNode* rootNode, const string& encodedText) {
    string result;
    HuffmanNode* currNode = rootNode;
    for (char bit : encodedText) {
        currNode = (bit == '0') ? currNode->leftChild : currNode->rightChild;
        if (!currNode->leftChild && !currNode->rightChild) {
            result += currNode->character;
            currNode = rootNode;
        }
    }
    return result;
}

// Save Huffman Tree
void writeHuffmanTree(HuffmanNode* rootNode, ofstream& outFile) {
    if (!rootNode) return;
    if (!rootNode->leftChild && !rootNode->rightChild) {
        outFile << '1' << rootNode->character;
    } else {
        outFile << '0';
    }
    writeHuffmanTree(rootNode->leftChild, outFile);
    writeHuffmanTree(rootNode->rightChild, outFile);
}

// Load Huffman Tree
HuffmanNode* readHuffmanTree(ifstream& inFile) {
    char marker;
    inFile >> noskipws >> marker;
    if (marker == '1') {
        char character;
        inFile >> character;
        return new HuffmanNode(character, 0);
    }
    HuffmanNode* newNode = new HuffmanNode('\0', 0);
    newNode->leftChild = readHuffmanTree(inFile);
    newNode->rightChild = readHuffmanTree(inFile);
    return newNode;
}

// Multithreaded decode
void threadedDecode(const string& encoded, HuffmanNode* rootNode, int begin, int finish, string& resultSegment) {
    string result;
    HuffmanNode* current = rootNode;
    for (int i = begin; i < finish; ++i) {
        current = (encoded[i] == '0') ? current->leftChild : current->rightChild;
        if (!current->leftChild && !current->rightChild) {
            result += current->character;
            current = rootNode;
        }
    }
    resultSegment = result;
}

// File compression
void compressDataFile(const string& sourceFile, const string& targetFile, int threadCount) {
    ifstream input(sourceFile);
    if (!input) {
        cout << "Error: Cannot open input file.\n";
        return;
    }

    string fileData((istreambuf_iterator<char>(input)), istreambuf_iterator<char>());
    input.close();

    unordered_map<char, int> freqMapMT, freqMapST;
    int slice = fileData.size() / threadCount;
    vector<thread> workers;

    auto mtStart = high_resolution_clock::now();
    for (int i = 0; i < threadCount; ++i) {
        int startIdx = i * slice;
        int endIdx = (i == threadCount - 1) ? fileData.size() : startIdx + slice;
        workers.emplace_back(countFrequencyThread, cref(fileData), ref(freqMapMT), startIdx, endIdx);
    }
    for (auto& th : workers) th.join();
    auto mtEnd = high_resolution_clock::now();
    double timeMT = duration_cast<nanoseconds>(mtEnd - mtStart).count() / 1e6;

    auto stStart = high_resolution_clock::now();
    countFrequencySingle(fileData, freqMapST);
    auto stEnd = high_resolution_clock::now();
    double timeST = duration_cast<nanoseconds>(stEnd - stStart).count() / 1e6;

    cout << "\n--- Compression Performance ---\n";
    cout << "Single-threaded time: " << timeST << " ms\n";
    cout << "Multi-threaded time:  " << timeMT << " ms\n";
    cout << "Speedup factor:       " << (timeST / timeMT) << "x\n";

    // Build Huffman Tree
    priority_queue<HuffmanNode*, vector<HuffmanNode*>, HuffmanCompare> minHeap;
    for (auto& pair : freqMapMT)
        minHeap.push(new HuffmanNode(pair.first, pair.second));
    while (minHeap.size() > 1) {
        HuffmanNode* left = minHeap.top(); minHeap.pop();
        HuffmanNode* right = minHeap.top(); minHeap.pop();
        HuffmanNode* merged = new HuffmanNode('\0', left->frequency + right->frequency);
        merged->leftChild = left; merged->rightChild = right;
        minHeap.push(merged);
    }

    HuffmanNode* root = minHeap.top();
    unordered_map<char, string> codeDict;
    createHuffmanCodes(root, "", codeDict);
    string encodedBinary = huffmanEncode(fileData, codeDict);

    ofstream output(targetFile, ios::binary);
    writeHuffmanTree(root, output);
    output << '\n' << encodedBinary;
    output.close();
    deleteHuffmanTree(root);

    cout << "Compression completed. Output saved to '" << targetFile << "'.\n";
}

// File decompression
void decompressDataFile(const string& sourceFile, const string& targetFile, int threadCount) {
    ifstream input(sourceFile, ios::binary);
    if (!input) {
        cout << "Error: Cannot open compressed file.\n";
        return;
    }

    HuffmanNode* root = readHuffmanTree(input);
    char temp;
    input >> noskipws >> temp;
    string binaryData((istreambuf_iterator<char>(input)), istreambuf_iterator<char>());
    input.close();

    // Single-threaded decode
    auto stStart = high_resolution_clock::now();
    string decodedST = huffmanDecode(root, binaryData);
    auto stEnd = high_resolution_clock::now();
    double timeST = duration_cast<nanoseconds>(stEnd - stStart).count() / 1e6;

    // Multi-threaded decode
    int slice = binaryData.size() / threadCount;
    vector<string> segments(threadCount);
    vector<thread> workers;

    auto mtStart = high_resolution_clock::now();
    for (int i = 0; i < threadCount; ++i) {
        int startIdx = i * slice;
        int endIdx = (i == threadCount - 1) ? binaryData.size() : startIdx + slice;
        workers.emplace_back(threadedDecode, cref(binaryData), root, startIdx, endIdx, ref(segments[i]));
    }
    for (auto& th : workers) th.join();
    auto mtEnd = high_resolution_clock::now();
    double timeMT = duration_cast<nanoseconds>(mtEnd - mtStart).count() / 1e6;

    string finalResult;
    for (auto& part : segments) finalResult += part;

    ofstream output(targetFile);
    output << finalResult;
    output.close();
    deleteHuffmanTree(root);

    cout << "\n--- Decompression Performance ---\n";
    cout << "Single-threaded time: " << timeST << " ms\n";
    cout << "Multi-threaded time:  " << timeMT << " ms\n";
    cout << "Speedup factor:       " << (timeST / timeMT) << "x\n";
    cout << "Decompression completed. Output saved to '" << targetFile << "'.\n";
}

// Main interface
int main() {
    int userChoice, threadNum;
    string inputFileName, outputFileName;

    cout << "------ Huffman Compressor & Decompressor with Metrics ------\n";
    cout << "1. Compress File\n2. Decompress File\nEnter your choice: ";
    cin >> userChoice;

    cout << "Enter input file name: ";
    cin >> inputFileName;
    cout << "Enter output file name: ";
    cin >> outputFileName;

    cout << "Enter number of threads to use: ";
    cin >> threadNum;

    if (userChoice == 1)
        compressDataFile(inputFileName, outputFileName, threadNum);
    else if (userChoice == 2)
        decompressDataFile(inputFileName, outputFileName, threadNum);
    else
        cout << "Invalid choice.\n";

    return 0;
}
