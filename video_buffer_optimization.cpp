#include <iostream>
#include <vector>
#include <algorithm>
#include <climits>
#include <iomanip>
#include <cmath>
#include <string>
using namespace std;

//Utility: Print a horizontal separator line 

void printLine(int width = 60, char ch = '-') {
    cout << "  " << string(width, ch) << "\n";
}

// Utility: Read a positive integer with validation 
// Keeps asking until the user enters a valid number > 0

int readPositiveInt(const string& prompt) {
    int value;
    while (true) {
        cout << prompt;
        cin >> value;

        // If stdin is closed (e.g. piped input ran out), exit gracefully
        if (cin.eof()) {
            cout << "\n  [End of input — exiting.]\n";
            exit(0);
        }

        // If the user typed something that isn't a number (e.g. "abc"),
        // cin goes into a fail state — we need to clear it and retry.
        if (cin.fail() || value <= 0) {
            cin.clear();
            cin.ignore(10000, '\n');
            cout << "  [!] Please enter a positive integer.\n";
        } else {
            return value;
        }
    }
}



//  CORE ALGORITHM 1: Playback Feasibility Check



bool canPlaySmoothly(const vector<int>& bandwidth, int bitrate, long long buffer) {
    for (int bw : bandwidth) {
        buffer += bw - bitrate;
        if (buffer < 0) return false;   // stall detected — buffer underflowed
    }
    return true;   // no stall at any point
}


//  CORE ALGORITHM 2: Binary Search for Minimum Buffer


long long findMinBufferBinarySearch(const vector<int>& bandwidth, int bitrate,
                                     bool showTrace = false) {
    int n = bandwidth.size();
    long long lo = 0;
    long long hi = (long long)n * bitrate;
    long long answer = hi;

    if (showTrace) {
        cout << "\n  Binary Search Trace:\n";
        cout << "  Initial search range: [" << lo << ", " << hi << "]\n\n";
    }

    int step = 1;
    while (lo <= hi) {
        long long mid = lo + (hi - lo) / 2;   // overflow-safe midpoint
        bool smooth = canPlaySmoothly(bandwidth, bitrate, mid);

        if (showTrace) {
            cout << "  Step " << setw(2) << step++ << ": buffer = " << setw(6) << mid
                 << " KB  ->  " << (smooth ? "SMOOTH" : "STALL ")
                 << "  ->  " << (smooth ? "try smaller" : "need more") << "\n";
        }

        if (smooth) {
            answer = mid;       // this works — but can we do with less?
            hi = mid - 1;       // search the lower half
        } else {
            lo = mid + 1;       // too small — search the upper half
        }
    }
    return answer;
}


//  COR E ALGORITHM 3: Closed-Form Optimal Solution



long long findMinBufferOptimal(const vector<int>& bandwidth, int bitrate) {
    long long runningSum = 0;
    long long worstDeficit = 0;   // tracks the deepest the buffer ever drops

    for (int bw : bandwidth) {
        runningSum += bw - bitrate;
        worstDeficit = min(worstDeficit, runningSum);
    }

    // If the worst deficit is negative, we need that much initial buffer.
    // If it's zero or positive, bandwidth always covered the bitrate.
    return max(0LL, -worstDeficit);
}


//  PLAYBACK SIMULATION: Visual second-by-second breakdown



void simulatePlayback(const vector<int>& bandwidth, int bitrate, long long initBuffer) {
    long long buffer = initBuffer;
    long long peakBuffer = initBuffer;
    int stallCount = 0;

    cout << "\n  Playback Simulation\n";
    cout << "  Initial Buffer: " << initBuffer << " KB | Bitrate: " << bitrate << " KB/s\n";
    printLine(70);
    cout << "  " << setw(4) << "Sec" << " | "
         << setw(8) << "BW(KB/s)" << " | "
         << setw(10) << "Net Delta" << " | "
         << setw(12) << "Buffer(KB)" << " | "
         << "Status\n";
    printLine(70);

    for (int i = 0; i < (int)bandwidth.size(); i++) {
        int delta = bandwidth[i] - bitrate;
        buffer += delta;

        // Track statistics
        peakBuffer = max(peakBuffer, buffer);
        if (buffer < 0) stallCount++;

        // Build a mini visual bar (each '#' = roughly 10 KB of buffer)
        int barLen = max(0, (int)(max(0LL, buffer) / 10));
        barLen = min(barLen, 20);   // cap bar length at 20 chars
        string bar(barLen, '#');

        // Determine status label
        string status;
        if (buffer < 0)       status = "STALL!";
        else if (delta > 0)   status = "filling  " + bar;
        else if (delta == 0)  status = "steady   " + bar;
        else                  status = "draining " + bar;

        cout << "  " << setw(4) << (i + 1) << " | "
             << setw(8) << bandwidth[i] << " | "
             << setw(10) << showpos << delta << noshowpos << " | "
             << setw(12) << max(0LL, buffer) << " | "
             << status << "\n";
    }

    printLine(70);
    cout << "  Final Buffer: " << max(0LL, buffer) << " KB"
         << " | Peak: " << peakBuffer << " KB"
         << " | Stalls: " << stallCount << "\n";
}


//  NETWORK ANALYSIS: Understand the bandwidth conditions
//


struct NetworkStats {
    double avgBandwidth;
    int minBandwidth;
    int maxBandwidth;
    double jitter;           // standard deviation of bandwidth
    int worstWindowStart;    // start index of worst k-second window
    int worstWindowSum;      // total bandwidth in that window
    double stabilityIndex;   // 0 = terrible, 100 = perfect
};

NetworkStats analyzeNetwork(const vector<int>& bandwidth, int bitrate, int windowSize = 3) {
    NetworkStats stats;
    int n = bandwidth.size();

    // ── Basic statistics ──
    long long total = 0;
    stats.minBandwidth = INT_MAX;
    stats.maxBandwidth = INT_MIN;

    for (int bw : bandwidth) {
        total += bw;
        stats.minBandwidth = min(stats.minBandwidth, bw);
        stats.maxBandwidth = max(stats.maxBandwidth, bw);
    }
    stats.avgBandwidth = (double)total / n;

    // ── Jitter (standard deviation) ──
    //    Measures how wildly bandwidth fluctuates second to second.
    //    High jitter = unpredictable network = needs bigger buffer.
    double variance = 0;
    for (int bw : bandwidth) {
        double diff = bw - stats.avgBandwidth;
        variance += diff * diff;
    }
    stats.jitter = sqrt(variance / n);

    // ── Worst bandwidth window (Sliding Window technique) ──
    //    Find the consecutive 'windowSize' seconds with lowest total bandwidth.
    //    This is the hardest period for the buffer to survive.
    windowSize = min(windowSize, n);   // clamp if stream is short

    // Calculate sum of first window
    int windowSum = 0;
    for (int i = 0; i < windowSize; i++) {
        windowSum += bandwidth[i];
    }
    stats.worstWindowSum = windowSum;
    stats.worstWindowStart = 0;

    // Slide the window across the array
    for (int i = windowSize; i < n; i++) {
        windowSum += bandwidth[i] - bandwidth[i - windowSize];  // slide right
        if (windowSum < stats.worstWindowSum) {
            stats.worstWindowSum = windowSum;
            stats.worstWindowStart = i - windowSize + 1;
        }
    }

    // ── Stability Index (0-100) ──
    //    Combines two factors:
    //      1. How much average bandwidth exceeds the bitrate (headroom)
    //      2. How low the jitter is relative to average bandwidth
    //    Higher = more stable = smaller buffer needed.
    double headroom = (stats.avgBandwidth > 0)
        ? min(1.0, (stats.avgBandwidth - bitrate) / stats.avgBandwidth)
        : 0.0;
    double consistency = (stats.avgBandwidth > 0)
        ? max(0.0, 1.0 - stats.jitter / stats.avgBandwidth)
        : 0.0;
    stats.stabilityIndex = max(0.0, min(100.0, (headroom * 50 + consistency * 50)));

    return stats;
}

void printNetworkAnalysis(const NetworkStats& stats, int bitrate, int windowSize = 3) {
    cout << "\n  Network Condition Analysis\n";
    printLine(55);

    cout << fixed << setprecision(1);
    cout << "  Average Bandwidth  : " << stats.avgBandwidth << " KB/s"
         << (stats.avgBandwidth >= bitrate ? "  (above bitrate)" : "  (BELOW bitrate!)") << "\n";
    cout << "  Bandwidth Range    : " << stats.minBandwidth << " - " << stats.maxBandwidth << " KB/s\n";
    cout << "  Network Jitter     : " << stats.jitter << " KB/s"
         << (stats.jitter > stats.avgBandwidth * 0.3 ? "  (HIGH — unstable!)" : "  (acceptable)") << "\n";

    cout << "  Worst " << windowSize << "-sec Window : seconds "
         << (stats.worstWindowStart + 1) << "-" << (stats.worstWindowStart + windowSize)
         << " (total: " << stats.worstWindowSum << " KB)\n";

    cout << "  Stability Index    : " << stats.stabilityIndex << " / 100";
    if (stats.stabilityIndex >= 80)      cout << "  [EXCELLENT]\n";
    else if (stats.stabilityIndex >= 50) cout << "  [MODERATE]\n";
    else if (stats.stabilityIndex >= 25) cout << "  [POOR]\n";
    else                                 cout << "  [CRITICAL]\n";

    printLine(55);
}


//  BUFFER RECOMMENDATION: Practical advice with safety margins


void printBufferRecommendation(long long optimalBuffer, const NetworkStats& stats) {
    cout << "\n  Buffer Recommendations\n";
    printLine(55);

    // The safety margin scales with network instability (jitter).
    // Stable network → small margin; chaotic network → big margin.
    double jitterFactor = (stats.avgBandwidth > 0)
        ? stats.jitter / stats.avgBandwidth
        : 1.0;

    long long safeBuffer   = optimalBuffer + (long long)(optimalBuffer * 0.2 * (1 + jitterFactor));
    long long robustBuffer = optimalBuffer + (long long)(optimalBuffer * 0.5 * (1 + jitterFactor));

    cout << "  Minimum (exact)    : " << optimalBuffer << " KB"
         << "  — zero margin, any extra dip causes stall\n";
    cout << "  Safe (+20% margin) : " << safeBuffer << " KB"
         << "  — handles minor fluctuations\n";
    cout << "  Robust (+50% margin): " << robustBuffer << " KB"
         << "  — handles major jitter and spikes\n";
    printLine(55);
}


//  TEST SUITE: Verify correctness against known answers

struct TestCase {
    vector<int> bandwidth;
    int bitrate;
    long long expected;       // -1 means "just display the result, no assertion"
    string description;
};

void runTests() {
    vector<TestCase> tests = {
        // ── Edge Cases ──
        {{100},                          100,  0,    "Single second, exactly at bitrate"},
        {{50},                           100,  50,   "Single second, below bitrate"},
        {{200},                          100,  0,    "Single second, above bitrate"},

        // ── Uniform Bandwidth ──
        {{100, 100, 100, 100},           100,  0,    "Perfectly stable connection"},
        {{200, 200, 200},                100,  0,    "Always surplus — no buffer needed"},
        {{30, 30, 30},                   100,  210,  "Always deficit — need full pre-load"},

        // ── Deficit then Recovery ──
        {{50, 50, 200},                  100,  100,  "Early dip, late recovery"},
        {{20, 20, 20, 200, 200},         100,  240,  "Deep early deficit, strong recovery"},

        // ── Alternating Pattern ──
        {{50, 150, 50, 150},             100,  50,   "Alternating low/high bandwidth"},

        // ── Gradual Decline ──
        {{200, 150, 100, 50, 10},        100,  0,    "Gradually worsening network"},

        // ── Real-world Simulation ──
        {{120,80,60,150,40,200,30,90},   100,  -1,   "Variable realistic scenario"},

        // ── Stress Test: All zeros ──
        {{0, 0, 0, 0, 0},               100,  500,  "Complete network outage"},
    };

    cout << "\n  ======================================================\n";
    cout << "                      TEST RESULTS                      \n";
    cout << "  ======================================================\n\n";

    cout << "  " << setw(3) << "#" << "  "
         << setw(10) << "Expected" << "  "
         << setw(10) << "Got" << "  "
         << setw(6) << "Status" << "  "
         << "Description\n";
    printLine(72);

    int passed = 0, total = 0;

    for (int i = 0; i < (int)tests.size(); i++) {
        auto& t = tests[i];
        long long got = findMinBufferOptimal(t.bandwidth, t.bitrate);

        // Also verify with binary search — both methods should agree
        long long gotBS = findMinBufferBinarySearch(t.bandwidth, t.bitrate, false);

        bool matchesExpected = (t.expected == -1) || (got == t.expected);
        bool methodsAgree = (got == gotBS);
        bool pass = matchesExpected && methodsAgree;

        total++;
        if (pass) passed++;

        // Format expected column
        string expectedStr = (t.expected == -1) ? "N/A" : to_string(t.expected) + " KB";

        cout << "  " << setw(3) << (i + 1) << "  "
             << setw(10) << expectedStr << "  "
             << setw(7) << got << " KB" << "  "
             << setw(6) << (pass ? "PASS" : "FAIL") << "  "
             << t.description;

        if (!methodsAgree) {
            cout << "  [MISMATCH: BS=" << gotBS << "]";
        }
        cout << "\n";
    }

    printLine(72);
    cout << "  Result: " << passed << "/" << total << " passed";
    if (passed == total) cout << "  — All tests passed!\n";
    else                 cout << "  — Some tests FAILED. Check logic.\n";
}


//  PRESET SCENARIOS: Common streaming configurations
//
//  Instead of manually entering numbers, the user can pick a realistic
//  streaming scenario to see how buffer optimization works in practice.

void runPresetScenario() {
    cout << "\n  Available Scenarios:\n";
    cout << "  1. Home Wi-Fi streaming (1080p, mild fluctuations)\n";
    cout << "  2. Mobile 4G streaming (720p, high variability)\n";
    cout << "  3. Congested network (480p, frequent drops)\n";
    cout << "  4. Fiber optic (4K, very stable)\n";

    int choice = readPositiveInt("\n  Select scenario (1-4): ");

    vector<int> bandwidth;
    int bitrate;
    string scenarioName;

    switch (choice) {
        case 1:
            scenarioName = "Home Wi-Fi — 1080p Streaming";
            bitrate = 500;   // 500 KB/s for 1080p
            bandwidth = {520, 480, 510, 450, 530, 400, 550, 500, 480, 520,
                         460, 510, 490, 530, 470, 500, 540, 430, 510, 500};
            break;
        case 2:
            scenarioName = "Mobile 4G — 720p Streaming";
            bitrate = 300;   // 300 KB/s for 720p
            bandwidth = {350, 200, 400, 150, 300, 250, 100, 380, 320, 180,
                         400, 220, 350, 160, 300, 280, 120, 390, 300, 200};
            break;
        case 3:
            scenarioName = "Congested Network — 480p Streaming";
            bitrate = 150;   // 150 KB/s for 480p
            bandwidth = {100, 80, 200, 50, 30, 180, 120, 60, 200, 40,
                         150, 70, 190, 50, 100, 80, 160, 30, 120, 90};
            break;
        case 4:
            scenarioName = "Fiber Optic — 4K Streaming";
            bitrate = 2500;  // 2500 KB/s for 4K
            bandwidth = {2600, 2550, 2500, 2580, 2520, 2490, 2600, 2530, 2500, 2550,
                         2480, 2560, 2510, 2590, 2500, 2540, 2500, 2570, 2520, 2500};
            break;
        default:
            cout << "  Invalid choice. Using scenario 1.\n";
            scenarioName = "Home Wi-Fi — 1080p Streaming";
            bitrate = 500;
            bandwidth = {520, 480, 510, 450, 530, 400, 550, 500, 480, 520};
    }

    cout << "\n  Scenario: " << scenarioName << "\n";
    cout << "  Duration: " << bandwidth.size() << " seconds | Bitrate: " << bitrate << " KB/s\n";

    // Run both algorithms
    long long bufferBS = findMinBufferBinarySearch(bandwidth, bitrate, true);
    long long bufferOpt = findMinBufferOptimal(bandwidth, bitrate);

    cout << "\n  Minimum Buffer (Binary Search) : " << bufferBS << " KB\n";
    cout << "  Minimum Buffer (Optimal)       : " << bufferOpt << " KB\n";
    cout << "  Methods agree                  : " << (bufferBS == bufferOpt ? "YES" : "NO") << "\n";

    // Network analysis
    NetworkStats stats = analyzeNetwork(bandwidth, bitrate);
    printNetworkAnalysis(stats, bitrate);

    // Recommendations
    printBufferRecommendation(bufferOpt, stats);

    // Simulation
    simulatePlayback(bandwidth, bitrate, bufferOpt);
}


//  CUSTOM INPUT: User enters their own bandwidth data

void runCustomInput() {
    int n = readPositiveInt("\n  Enter number of seconds: ");
    int bitrate = readPositiveInt("  Enter video bitrate (KB/s): ");

    vector<int> bandwidth(n);
    cout << "  Enter bandwidth for each second (KB/s):\n";
    for (int i = 0; i < n; i++) {
        string prompt = "    Second " + to_string(i + 1) + ": ";
        bandwidth[i] = readPositiveInt(prompt);
    }

    // ── Run Binary Search (with trace) ──
    cout << "\n  [Approach 1] Binary Search + Linear Simulation\n";
    long long ans1 = findMinBufferBinarySearch(bandwidth, bitrate, true);
    cout << "\n  Result: Minimum buffer = " << ans1 << " KB\n";

    // ── Run Optimal O(n) ──
    cout << "\n  [Approach 2] Closed-Form O(n) Solution\n";
    long long ans2 = findMinBufferOptimal(bandwidth, bitrate);
    cout << "  Result: Minimum buffer = " << ans2 << " KB\n";

    // ── Verify agreement ──
    cout << "\n  Both approaches agree: " << (ans1 == ans2 ? "YES" : "NO — BUG!") << "\n";

    // ── Network Analysis ──
    NetworkStats stats = analyzeNetwork(bandwidth, bitrate);
    printNetworkAnalysis(stats, bitrate);

    // ── Buffer Recommendations ──
    printBufferRecommendation(ans2, stats);

    // ── Visual Simulation ──
    simulatePlayback(bandwidth, bitrate, ans2);
}


//  MAIN: Interactive menu

int main() {
    ios_base::sync_with_stdio(false);
    cin.tie(NULL);

    cout << "\n";
    cout << "  ======================================================\n";
    cout << "    Case Study 5: Video Streaming Buffer Optimization    \n";
    cout << "  ======================================================\n";
    cout << "  Objective: Find minimum buffer for stall-free playback \n";
    cout << "  Methods  : Binary Search + O(n) Closed-Form Solution  \n";
    cout << "  ======================================================\n";

    bool running = true;
    while (running) {
        cout << "\n  ── Main Menu ──\n";
        cout << "  1. Enter custom bandwidth data\n";
        cout << "  2. Run a preset streaming scenario\n";
        cout << "  3. Run automated test suite\n";
        cout << "  4. Show complexity analysis\n";
        cout << "  5. Exit\n";

        int choice = readPositiveInt("\n  Select option (1-5): ");

        switch (choice) {
            case 1:
                runCustomInput();
                break;

            case 2:
                runPresetScenario();
                break;

            case 3:
                runTests();
                break;

            case 4:
                cout << "\n  ── Algorithm Complexity Summary ──\n";
                printLine(55);
                cout << "  Binary Search Approach:\n";
                cout << "    Time  : O(n * log(n * B))\n";
                cout << "            n = stream duration, B = bitrate\n";
                cout << "            Binary search does log(n*B) iterations,\n";
                cout << "            each running an O(n) simulation.\n";
                cout << "    Space : O(1) — only a few variables\n\n";
                cout << "  Closed-Form Optimal:\n";
                cout << "    Time  : O(n) — single pass through bandwidth array\n";
                cout << "    Space : O(1) — just a running sum and min tracker\n\n";
                cout << "  Network Analysis:\n";
                cout << "    Time  : O(n) — statistics + sliding window in one pass\n";
                cout << "    Space : O(1)\n";
                printLine(55);
                cout << "  Both methods produce identical results.\n";
                cout << "  The closed-form is faster; binary search is more\n";
                cout << "  intuitive and easier to explain in interviews.\n";
                break;

            case 5:
                running = false;
                cout << "\n  Exiting. Happy streaming!\n\n";
                break;

            default:
                cout << "  Invalid option. Please select 1-5.\n";
        }
    }

    return 0;
}
