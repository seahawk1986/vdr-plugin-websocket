#pragma once

struct tWebsocketConfig
{
    int ShowDebug; // Als int definieren, da VDR-Menüs int* bevorzugen
};

extern tWebsocketConfig WebsocketConfig;