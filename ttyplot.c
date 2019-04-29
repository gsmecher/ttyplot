//
// ttyplot: a realtime plotting utility for terminal with data input from stdin
// Copyright (c) 2018-2019 by Antoni Sawicki
// Copyright (c) 2019 by Google LLC
// Apache License 2.0
//

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <float.h>
#include <time.h>
#include <curses.h>
#include <signal.h>

#define verstring "github.com/tenox7/ttyplot 1.2"

#ifdef NOACS
#define T_HLINE '-'
#define T_VLINE '|'
#define T_RARR '>'
#define T_UARR '^'
#define T_LLCR 'L'
#else
#define T_HLINE ACS_HLINE
#define T_VLINE ACS_VLINE
#define T_RARR ACS_RARROW
#define T_UARR ACS_UARROW
#define T_LLCR ACS_LLCORNER
#endif

void usage() {
    printf("Usage:\n ttyplot [-2] [-r] [-c plotchar] [-s softmax] [-m hardmax] [-t title] [-u unit]\n\n"
            "-2 read two values and draw two plots, the second one is in reverse video\n\n"
            "-r calculate counter rate and divide by measured sample interval\n\n"
            "-c character to use for plot line, eg @ # %% . etc\n\n"
            "-C character to use for plot line when value exceeds max (default: x)\n\n"
            "-s softmax is an initial maximum value that can grow if data input has larger value\n\n"
            "-m hardmax is a hard maximum value that can never grow, \n"
            "   if data input has larger value the plot line will not be drawn\n\n"
            "-t title of the plot\n\n"
            "-u unit displayed beside vertical bar\n\n");
    exit(0);
}

void getminmax(int pw, int n, double *values, double *min, double *max, double *avg) {
    double tot=0;
    int i=0;

    *min=FLT_MAX;
    *max=FLT_MIN;
    tot=FLT_MIN;

    for(i=0; i<pw; i++) 
       if(values[i]>*max)
            *max=values[i];
    
    for(i=0; i<pw; i++) 
        if(values[i]<*min)
            *min=values[i];

    for(i=0; i<pw; i++) 
        tot=tot+values[i];

    *avg=tot/pw;
}


void draw_axes(int h, int w, int ph, int pw, double max, char *unit) {
    mvhline(h-3, 2, T_HLINE, pw);
    mvaddch(h-3, 2+pw, T_RARR);

    mvvline(2, 2, T_VLINE, ph);
    mvaddch(1, 2, T_UARR);

    mvaddch(h-3, 2, T_LLCR);

    mvprintw(1, 4, "%.1f %s", max, unit);
    mvprintw((ph/4)+1, 4, "%.1f %s", max*3/4, unit);
    mvprintw((ph/2)+1, 4, "%.1f %s", max/2, unit);
    mvprintw((ph*3/4)+1, 4, "%.1f %s", max/4, unit);
}

void clip_bar(int ph, int *y, int *l, chtype *plotchar, chtype clipchar) {
  if (*y < 1 && clipchar) {
    *y = 1;
    *l = ph;
    *plotchar = clipchar;
  }
}

void draw_line(int ph, int l1, int l2, int x, chtype plotchar, chtype clipchar) {
    int y1=ph+1-l1, y2=ph+1-l2;
    chtype char1=plotchar, char2=(l1 < l2) ? ' ' : plotchar;
    clip_bar(ph, &y1, &l1, &char1, clipchar);
    clip_bar(ph, &y2, &l2, &char2, clipchar);
    if(l1 > l2) {
        mvvline(y1, x, char1, l1-l2 );
        mvvline(y2, x, char2|A_REVERSE, l2 );
    } else if(l1 < l2) {
        mvvline(y2, x, char2|A_REVERSE,  l2-l1 );
        mvvline(y1, x, char1|A_REVERSE, l1 );
    } else {
        mvvline(y2, x, char2|A_REVERSE, l2 );
    }

}


void draw_values(int h, int w, int ph, int pw, double *v1, double *v2, double max, int n, chtype plotchar, chtype clipchar) {
    int i;
    int x=3;
    int l1=0, l2=0;

    for(i=n+1; i<pw; i++) {
        l1=(int)((v1[i]/max)*(double)ph);
        l2=(int)((v2[i]/max)*(double)ph);
        draw_line(ph, l1, l2, x++, plotchar, clipchar);
    }

    for(i=0; i<=n; i++) {
        l1=(int)((v1[i]/max)*(double)ph);
        l2=(int)((v2[i]/max)*(double)ph);
        draw_line(ph, l1, l2, x++, plotchar, clipchar);
    }

}

void resize(int sig) {
    endwin();
    refresh();
}

void finish(int sig) {
    curs_set(FALSE);
    echo();
    refresh();
    endwin();
    exit(0);
}

int main(int argc, char *argv[]) {
    double values1[1024]={0};
    double values2[1024]={0};
    double cval1=FLT_MAX, pval1=FLT_MAX;
    double cval2=FLT_MAX, pval2=FLT_MAX;
    double min1=FLT_MAX, max1=FLT_MIN, avg1=0;
    double min2=FLT_MAX, max2=FLT_MIN, avg2=0;
    int n=0;
    int r=0;
    int width=0, height=0;
    int plotwidth=0, plotheight=0;
    time_t t1,t2,td;
    struct tm *lt;
    int c;
    chtype plotchar=T_VLINE, clipchar='x';
    double max=FLT_MIN;
    double softmax=FLT_MIN;
    double hardmax=FLT_MIN;
    char title[256]=".: ttyplot :.";
    char unit[64]={0};
    char ls[256]={0};
    int rate=0;
    int two=0;
    
    
    opterr=0;
    while((c=getopt(argc, argv, "2rc:C:s:m:t:u:")) != -1)
        switch(c) {
            case 'r':
                rate=1;
                break;
            case '2':
                two=1;
                plotchar='|';
                break;
            case 'c':
                plotchar=optarg[0];
                break;
            case 'C':
                clipchar=optarg[0];
                break;
            case 's':
                softmax=atof(optarg);
                break;
            case 'm':
                hardmax=atof(optarg);
                break;
            case 't':
                snprintf(title, sizeof(title), "%s", optarg);
                break;
            case 'u':
                snprintf(unit, sizeof(unit), "%s", optarg);
                break;
            case '?':
                usage();
                break;
        }

    time(&t1);
    initscr();
    noecho();
    curs_set(FALSE);
    signal(SIGWINCH, (void*)resize);
    signal(SIGINT, (void*)finish);

    erase();
    getmaxyx(stdscr, height, width);
    mvprintw(height/2, (width/2)-14, "waiting for data from stdin");
    refresh();
    
    while(1) {
        if(two)
            r=scanf("%lf %lf", &values1[n], &values2[n]);
        else
            r=scanf("%lf", &values1[n]);
        if(r==0) {
            while(getchar()!='\n');
            continue;
        }
        else if(r<0) {
            break;
        } 

        if(values1[n] < 0)
            values1[n] = 0;
        if(values2[n] < 0)
            values2[n] = 0;

        if(rate) {
            t2=t1;
            time(&t1);
            td=t1-t2;
            if(td==0)
                td=1;

            if(cval1==FLT_MAX) 
                pval1=values1[n];
            else
                pval1=cval1;
            cval1=values1[n];
            
            values1[n]=(cval1-pval1)/td;

            if(values1[n] < 0) // counter rewind
                values1[n]=0;

            if(two) {
                if(cval2==FLT_MAX) 
                    pval2=values2[n];
                else
                    pval2=cval2;
                cval2=values2[n];
                
                values2[n]=(cval2-pval2)/td;

                if(values2[n] < 0) // counter rewind
                    values2[n]=0;
            }
        } else {
            time(&t1);
        }

        erase();
        getmaxyx(stdscr, height, width);
        plotheight=height-4;
        plotwidth=width-4;
        if(plotwidth>=(sizeof(values1)/sizeof(double))-1)
            return 0;
        

        getminmax(plotwidth, n, values1, &min1, &max1, &avg1);
        getminmax(plotwidth, n, values2, &min2, &max2, &avg2);

        if(max1>max2)
            max=max1;
        else
            max=max2;

        if(max<softmax)
            max=softmax;
        if(hardmax!=FLT_MIN)
            max=hardmax;

        mvprintw(height-1, width-sizeof(verstring)/sizeof(char), verstring);

        lt=localtime(&t1);
        asctime_r(lt, ls);
        mvprintw(height-2, width-strlen(ls), "%s", ls);
        
        mvvline(height-2, 5, plotchar|A_NORMAL, 1);
        mvprintw(height-2, 7, "last=%.1f min=%.1f max=%.1f avg=%.1f %s ",  values1[n], min1, max1, avg1, unit);
        if(rate)
            printw(" interval=%ds", td);

        if(two) {
            mvaddch(height-1, 5, ' '|A_REVERSE);
            mvprintw(height-1, 7, "last=%.1f min=%.1f max=%.1f avg=%.1f %s   ",  values2[n], min2, max2, avg2, unit);
        }

        draw_values(height, width, plotheight, plotwidth, values1, values2, max, n, plotchar, clipchar);

        draw_axes(height, width, plotheight, plotwidth, max, unit);

        mvprintw(0, (width/2)-(strlen(title)/2), "%s", title);


        if(n<(plotwidth)-1)
            n++;
        else
            n=0;

        move(0,0);            
        refresh();
    }

    endwin();

    return 0;
}
