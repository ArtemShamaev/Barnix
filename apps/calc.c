#include <stdio.h>

int main(void)
{
    int num1;
    int num2;

    if (argument_count() != 4 ||
        argument_int(1, &num1) != 0 ||
        argument_int(3, &num2) != 0) {

        puts("Usage: calc NUM OPERATOR NUM");
        return 1;
    }

    const char *op = argument_value(2);
    char operator = op && op[0] ? op[0] : 0;

    int result;

    switch (operator) {
        case '+':
            result = num1 + num2;
            break;

        case '-':
            result = num1 - num2;
            break;

        case '*':
            result = num1 * num2;
            break;

        case '/':
            if (num2 == 0) {
                puts("Error: division by zero");
                return 1;
            }

            result = num1 / num2;
            break;

        case '%':
            if (num2 == 0) {
                puts("Error: division by zero");
                return 1;
            }

            result = num1 % num2;
            break;

        default:
            puts("Error: unknown operator");
            return 1;
    }

    printf("Result: %d\n", result);

    return 0;
}
