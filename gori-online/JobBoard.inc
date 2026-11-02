// ---------------------------------------------------------------- a job's results board (like GTA Online's leaderboard)
// The job's name, a line under it, a header in the job's colour and a row for each player, yours highlighted: the same
// layout as the lobby's results (Corona.inc). Columns as fm_race_controler.c's: POSITION, then the job's own (a race:
// VEHICLE, BEST LAP, TOTAL TIME, RP, CASH). A column is its name and its centre, as a fraction of the board's width.
struct JobBoardRow { std::vector<std::string> cells; bool me = false; };
static const int JB_RACE[3] = {197, 44, 132}, JB_VERSUS[3] = {178, 40, 40}, JB_SURVIVAL[3] = {72, 140, 60};
static void JobBoard(const std::string &title, const std::string &sub, const int *colour, const std::vector<std::pair<std::string, float>> &cols,
                     const std::vector<JobBoardRow> &rows, int cashCol = -1) {
    N(N_HIDE_HUD_AND_RADAR_THIS_FRAME);
    const float L = 0.162f, w = 0.679f;
    float y = 0.185f;
    Rect(0.5f, 0.5f, 1.f, 1.f, 0, 0, 0, 110);
    Text(title, L, 0.085f, 0.75f, 4, 255, 255, 255, 255, 1);
    if (!sub.empty()) Text(sub, L, 0.135f, 0.4f, 0, 255, 255, 255, 255, 1);
    Rect(L + w / 2, y + 0.0145f, w, 0.029f, colour[0], colour[1], colour[2], 255);
    for (size_t c = 0; c < cols.size(); ++c)
        Text(cols[c].first, c ? L + w * cols[c].second : L + 0.006f, y + 0.004f, 0.3f, 0, 255, 255, 255, 255, c ? 0 : 1);
    y += 0.031f;
    for (const JobBoardRow &r : rows) {
        Rect(L + w / 2, y + 0.0185f, w, 0.035f, r.me ? colour[0] : 0, r.me ? colour[1] : 0, r.me ? colour[2] : 0, r.me ? 220 : 170);
        for (size_t c = 0; c < cols.size() && c < r.cells.size(); ++c) {
            bool cash = (int)c == cashCol && r.cells[c] != "-";
            Text(r.cells[c], c ? L + w * cols[c].second : L + 0.006f, y + 0.007f, 0.32f, 0, cash ? 114 : 255, cash ? 204 : 255, cash ? 114 : 255, 255, c ? 0 : 1);
        }
        y += 0.037f;
    }
}
