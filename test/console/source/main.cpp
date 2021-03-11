﻿#include "pch.h"

void run_input_device_events();
void run_test_app();

int main()
{
    std::cout << "Choose:" << std::endl
        << "1) Input device events" << std::endl
        << "2) Test app" << std::endl;

    int choice = 0;
    std::cin >> choice;

    switch (choice)
    {
        case 1:
            ::run_input_device_events();
            break;

        case 2:
            ::run_test_app();
            break;
    }

    return 0;
}
