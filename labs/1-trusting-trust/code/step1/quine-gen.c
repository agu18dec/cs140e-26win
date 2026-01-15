// convert the contents of stdin to their ASCII values (e.g., 
// '\n' = 10) and spit out the <prog> array used in Figure 1 in
// Thompson's paper.
#include <stdio.h>

int main(void) { 
    // put your code here.
    int c = 0;
    int i = 0;
    char prog[100000];
    while ((c = getchar()) != EOF) {
        prog[i++] = c;
    }
    prog[i] = 0;
    printf("char prog[] = {\n");
	for(i = 0; prog[i]; i++)
		printf("\t%d,%c", prog[i], (i+1)%8==0 ? '\n' : ' ');
    printf("0 };\n");
    printf("%s", prog);
	return 0;
}
