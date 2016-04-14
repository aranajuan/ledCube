#include <string.h>
#include <stdio.h>
#include <math.h>

int isnumber(char c){
	return (c>='0' && c<='9');
}

int ishex(char c){
	return (c>='A' && c<='F');
}

void main (int argc,char * argv[]){
	int pos=0;
	int ignoring=0;
	int weights[8]={16,1};
	char c;
	FILE *fp;
	FILE *dest;
	unsigned char destChar;
	int destCount;
	/*open file*/
	fp=fopen(argv[1],"r");
	if(!fp){
		printf("Error al abrir archivo");
		return;
	}
	dest=fopen("INDEX.CBP","wb");
	
	/*convert*/
	c= getc(fp);
	destCount=0;
	destChar=0;
	while(c!=EOF){
		pos++;
		if(c=='/'){
			ignoring=!ignoring;
		}
		if(!ignoring){
			if(isnumber(c)){
				destChar+= weights[destCount]*(c-'0');
				destCount++;
			}else{
				if(ishex(c)){
					destChar+= weights[destCount]*(c-'A'+10);
					destCount++;
				}
			}

			if(destCount==2){
				destCount=0;
				fwrite(&destChar, 1, sizeof(char), dest);
				destChar=0;
			}
		}
		c= getc(fp);
	}
	fclose(fp);
	fclose(dest);
}

