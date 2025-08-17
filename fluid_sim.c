#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>

// Constants
#define MAX_PARTICLES 800
#define NUM_VERTICAL_CELLS 23
#define NUM_HORIZONTAL_CELLS 23
#define MAX_PARTICLES_X2 (MAX_PARTICLES * 2)
#define NUM_CELLS (NUM_VERTICAL_CELLS * NUM_HORIZONTAL_CELLS)
#define NUM_CELLS_X2 (NUM_CELLS * 2)
#define NUM_CELLS_PLUS1 (NUM_CELLS + 1)

#define SIM_HEIGHT 23.0f
#define SIM_WIDTH 23.0f

#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600
#define GRID_SIZE 20

// Enums
typedef enum {
    AIR_CELL,
    FLUID_CELL,
    SOLID_CELL
} CellType;

// Structures
typedef struct {
    float density;
    float fNumX;
    float fNumY;
    float h;
    float fInvSpacing;
    float fNumCells;
    
    float u[NUM_CELLS_X2];
    float v[NUM_CELLS_X2];
    float du[NUM_CELLS_X2];
    float dv[NUM_CELLS_X2];
    float prevU[NUM_CELLS_X2];
    float prevV[NUM_CELLS_X2];
    
    float p[NUM_CELLS];
    float s[NUM_CELLS];
    CellType cellType[NUM_CELLS];
    
    int maxParticles;
    float particlePos[MAX_PARTICLES_X2];
    float particleVel[MAX_PARTICLES_X2];
    float particleDensity[NUM_CELLS];
    float particleRestDensity;
    float particleRadius;
    float pInvSpacing;
    int pNumX;
    int pNumY;
    int pNumCells;
    
    int numCellParticles[NUM_CELLS];
    int firstCellParticle[NUM_CELLS_PLUS1];
    int cellParticleIds[MAX_PARTICLES];
    int numParticles;
} FlipFluid;

typedef struct {
    float xGravity;
    float yGravity;
    float dt;
    float flipRatio;
    int numPressureIters;
    int numParticleIters;
    int frameNr;
    float overRelaxation;
    bool compensateDrift;
    bool separateParticles;
    bool paused;
    bool smoothMode;  // true = スムースモード, false = クラシックモード
    FlipFluid fluid;
} Scene;

// Utility functions
float clampf(float x, float min, float max) {
    if (x < min) return min;
    else if (x > max) return max;
    else return x;
}

int clampi(int x, int min, int max) {
    if (x < min) return min;
    else if (x > max) return max;
    else return x;
}

// FlipFluid functions
void initFlipFluid(FlipFluid* fluid, float density, float width, float height, 
                   float spacing, float particleRadius, int maxParticles) {
    fluid->density = density;
    fluid->fNumX = floorf(width / spacing);
    fluid->fNumY = floorf(height / spacing);
    fluid->h = fmaxf(width / fluid->fNumX, height / fluid->fNumY);
    fluid->fInvSpacing = 1.0f / fluid->h;
    fluid->fNumCells = fluid->fNumX * fluid->fNumY;
    
    // Initialize arrays
    memset(fluid->u, 0, sizeof(fluid->u));
    memset(fluid->v, 0, sizeof(fluid->v));
    memset(fluid->du, 0, sizeof(fluid->du));
    memset(fluid->dv, 0, sizeof(fluid->dv));
    memset(fluid->prevU, 0, sizeof(fluid->prevU));
    memset(fluid->prevV, 0, sizeof(fluid->prevV));
    memset(fluid->p, 0, sizeof(fluid->p));
    memset(fluid->s, 0, sizeof(fluid->s));
    
    for (int i = 0; i < NUM_CELLS; i++) {
        fluid->cellType[i] = AIR_CELL;
    }
    
    fluid->maxParticles = maxParticles;
    
    // Initialize particles in a grid pattern
    int count = 0;
    for (int i = 1; i < 21 && count < MAX_PARTICLES; i++) {
        for (int j = 1; j < 21 && count < MAX_PARTICLES; j++) {
            fluid->particlePos[count * 2] = (float)j / 2.0f;
            fluid->particlePos[count * 2 + 1] = (float)i / 2.0f;
            count++;
        }
    }
    
    memset(fluid->particleVel, 0, sizeof(fluid->particleVel));
    memset(fluid->particleDensity, 0, sizeof(fluid->particleDensity));
    
    fluid->particleRestDensity = 0.0f;
    fluid->particleRadius = particleRadius;
    fluid->pInvSpacing = 1.0f;
    fluid->pNumX = (int)floorf(width * fluid->pInvSpacing);
    fluid->pNumY = (int)floorf(height * fluid->pInvSpacing);
    fluid->pNumCells = fluid->pNumX * fluid->pNumY;
    
    memset(fluid->numCellParticles, 0, sizeof(fluid->numCellParticles));
    memset(fluid->firstCellParticle, 0, sizeof(fluid->firstCellParticle));
    memset(fluid->cellParticleIds, 0, sizeof(fluid->cellParticleIds));
    
    fluid->numParticles = 0;
}

void integrateParticles(FlipFluid* fluid, float dt, float yGravity, float xGravity) {
    for (int i = 0; i < fluid->numParticles; i++) {
        fluid->particleVel[2 * i] += dt * xGravity;
        fluid->particleVel[2 * i + 1] += dt * yGravity;
        fluid->particlePos[2 * i] += fluid->particleVel[2 * i] * dt;
        fluid->particlePos[2 * i + 1] += fluid->particleVel[2 * i + 1] * dt;
    }
}

void showParticles(FlipFluid* fluid, bool smoothMode) {
    // Clear all non-solid cells first
    for (int i = 0; i < NUM_CELLS; i++) {
        if (fluid->cellType[i] != SOLID_CELL) {
            fluid->cellType[i] = AIR_CELL;
        }
    }
    
    if (smoothMode) {
        // スムースモード: 近隣セルも考慮
        int particleCount[NUM_CELLS] = {0};
        
        // Count particles per cell
        for (int i = 0; i < fluid->numParticles; i++) {
            float x = fluid->particlePos[2 * i];
            float y = fluid->particlePos[2 * i + 1];
            
            int cell_x = (int)floorf(x);
            int cell_y = (int)floorf(y);
            
            if (cell_x >= 0 && cell_x < 23 && cell_y >= 0 && cell_y < 23) {
                int cellIndex = cell_y * 23 + cell_x;
                if (cellIndex >= 0 && cellIndex < NUM_CELLS) {
                    particleCount[cellIndex]++;
                }
            }
        }
        
        // Mark cells with particles as FLUID_CELL and apply smoothing
        for (int i = 1; i < 22; i++) {
            for (int j = 1; j < 22; j++) {
                int cellIndex = i * 23 + j;
                
                // Check if this cell or neighboring cells have particles
                int totalParticles = 0;
                for (int di = -1; di <= 1; di++) {
                    for (int dj = -1; dj <= 1; dj++) {
                        int ni = i + di;
                        int nj = j + dj;
                        if (ni >= 0 && ni < 23 && nj >= 0 && nj < 23) {
                            int neighborIndex = ni * 23 + nj;
                            totalParticles += particleCount[neighborIndex];
                        }
                    }
                }
                
                // If there are enough particles in the neighborhood, mark as fluid
                if (totalParticles > 0) {
                    fluid->cellType[cellIndex] = FLUID_CELL;
                }
            }
        }
    } else {
        // クラシックモード: パーティクルがあるセルのみ
        for (int i = 0; i < fluid->numParticles; i++) {
            float x = fluid->particlePos[2 * i];
            float y = fluid->particlePos[2 * i + 1];
            
            int cell_x = (int)floorf(x);
            int cell_y = (int)floorf(y);
            
            if (cell_x >= 0 && cell_x < 23 && cell_y >= 0 && cell_y < 23) {
                int cellIndex = cell_y * 23 + cell_x;
                if (cellIndex >= 0 && cellIndex < NUM_CELLS) {
                    fluid->cellType[cellIndex] = FLUID_CELL;
                }
            }
        }
    }
}

void pushParticlesApart(FlipFluid* fluid, int numIters) {
    memset(fluid->numCellParticles, 0, sizeof(fluid->numCellParticles));
    
    // Count particles per cell
    for (int i = 0; i < fluid->numParticles; i++) {
        float x = fluid->particlePos[2 * i];
        float y = fluid->particlePos[2 * i + 1];
        
        int xi = clampi((int)floorf(x), 1, fluid->pNumX - 2);
        int yi = clampi((int)floorf(y), 1, fluid->pNumY - 2);
        int cellNr = xi * fluid->pNumY + yi;
        fluid->numCellParticles[cellNr]++;
    }
    
    // Partial sums
    int first = 0;
    for (int i = 0; i < fluid->pNumCells; i++) {
        first += fluid->numCellParticles[i];
        fluid->firstCellParticle[i] = first;
    }
    fluid->firstCellParticle[fluid->pNumCells] = first;
    
    // Fill particles into cells
    for (int i = 0; i < fluid->numParticles; i++) {
        float x = fluid->particlePos[2 * i];
        float y = fluid->particlePos[2 * i + 1];
        
        int xi = clampi((int)floorf(x * fluid->pInvSpacing), 1, fluid->pNumX - 2);
        int yi = clampi((int)floorf(y * fluid->pInvSpacing), 1, fluid->pNumY - 2);
        int cellNr = xi * fluid->pNumY + yi;
        fluid->firstCellParticle[cellNr]--;
        fluid->cellParticleIds[fluid->firstCellParticle[cellNr]] = i;
    }
    
    // Push particles apart
    float minDist = 2.0f * fluid->particleRadius;
    float minDist2 = minDist * minDist;
    
    for (int iter = 0; iter < numIters; iter++) {
        for (int i = 0; i < fluid->numParticles; i++) {
            float px = fluid->particlePos[2 * i];
            float py = fluid->particlePos[2 * i + 1];
            
            int pxi = (int)floorf(px * fluid->pInvSpacing);
            int pyi = (int)floorf(py * fluid->pInvSpacing);
            int x0 = fmaxf(pxi - 1, 0);
            int y0 = fmaxf(pyi - 1, 0);
            int x1 = fminf(pxi + 1, fluid->pNumX - 1);
            int y1 = fminf(pyi + 1, fluid->pNumY - 1);
            
            for (int xi = x0; xi <= x1; xi++) {
                for (int yi = y0; yi <= y1; yi++) {
                    int cellNr = xi * fluid->pNumY + yi;
                    int first = fluid->firstCellParticle[cellNr];
                    int last = fluid->firstCellParticle[cellNr + 1];
                    
                    for (int j = first; j < last; j++) {
                        int id = fluid->cellParticleIds[j];
                        if (id == i) continue;
                        
                        float qx = fluid->particlePos[2 * id];
                        float qy = fluid->particlePos[2 * id + 1];
                        
                        float dx = qx - px;
                        float dy = qy - py;
                        float d2 = dx * dx + dy * dy;
                        
                        if (d2 > minDist2 || d2 == 0.0f) continue;
                        
                        float d = sqrtf(d2);
                        float s = 0.5f * (minDist - d) / d;
                        dx *= s;
                        dy *= s;
                        
                        fluid->particlePos[2 * i] -= dx;
                        fluid->particlePos[2 * i + 1] -= dy;
                        fluid->particlePos[2 * id] += dx;
                        fluid->particlePos[2 * id + 1] += dy;
                    }
                }
            }
        }
    }
}

void handleParticleCollisions(FlipFluid* fluid) {
    float minX = 1.0f;
    float maxX = 21.0f;
    float minY = 1.0f;
    float maxY = 21.0f;
    
    for (int i = 0; i < fluid->numParticles; i++) {
        float x = fluid->particlePos[2 * i];
        float y = fluid->particlePos[2 * i + 1];
        
        if (x < minX) {
            x = minX;
            fluid->particleVel[2 * i] = 0.0f;
        }
        if (x > maxX) {
            x = maxX;
            fluid->particleVel[2 * i] = 0.0f;
        }
        if (y < minY) {
            y = minY;
            fluid->particleVel[2 * i + 1] = 0.0f;
        }
        if (y > maxY) {
            y = maxY;
            fluid->particleVel[2 * i + 1] = 0.0f;
        }
        
        fluid->particlePos[2 * i] = x;
        fluid->particlePos[2 * i + 1] = y;
    }
}

void updateParticleDensity(FlipFluid* fluid) {
    float n = fluid->fNumY;
    float h = fluid->h;
    float h1 = fluid->fInvSpacing;
    float h2 = 0.5f * h;
    
    memset(fluid->particleDensity, 0, sizeof(fluid->particleDensity));
    
    for (int i = 0; i < fluid->numParticles; i++) {
        float x = fluid->particlePos[2 * i];
        float y = fluid->particlePos[2 * i + 1];
        
        x = clampf(x, h, (fluid->fNumX - 1.0f) * h);
        y = clampf(y, h, (fluid->fNumY - 1.0f) * h);
        
        float x0 = floorf((x - h2) * h1);
        float tx = ((x - h2) - x0 * h) * h1;
        float x1 = fminf(x0 + 1.0f, fluid->fNumX - 2.0f);
        float y0 = floorf((y - h2) * h1);
        float ty = ((y - h2) - y0 * h) * h1;
        float y1 = fminf(y0 + 1.0f, fluid->fNumY - 2.0f);
        
        float sx = 1.0f - tx;
        float sy = 1.0f - ty;
        
        if (x0 < fluid->fNumX && y0 < fluid->fNumY) 
            fluid->particleDensity[(int)(x0 * n + y0)] += sx * sy;
        if (x1 < fluid->fNumX && y0 < fluid->fNumY) 
            fluid->particleDensity[(int)(x1 * n + y0)] += tx * sy;
        if (x1 < fluid->fNumX && y1 < fluid->fNumY) 
            fluid->particleDensity[(int)(x1 * n + y1)] += tx * ty;
        if (x0 < fluid->fNumX && y1 < fluid->fNumY) 
            fluid->particleDensity[(int)(x0 * n + y1)] += sx * ty;
    }
    
    if (fluid->particleRestDensity == 0.0f) {
        float sum = 0.0f;
        int numFluidCells = 0;
        
        for (int i = 0; i < (int)fluid->fNumCells; i++) {
            if (fluid->cellType[i] == FLUID_CELL) {
                sum += fluid->particleDensity[i];
                numFluidCells++;
            }
        }
        
        if (numFluidCells > 0) {
            fluid->particleRestDensity = sum / (float)numFluidCells;
        }
    }
}

void transferVelocities(FlipFluid* fluid, bool toGrid, float flipRatio) {
    float n = fluid->fNumY;
    float h = fluid->h;
    float h1 = fluid->fInvSpacing;
    float h2 = 0.5f * h;
    
    if (toGrid) {
        memcpy(fluid->prevU, fluid->u, sizeof(fluid->u));
        memcpy(fluid->prevV, fluid->v, sizeof(fluid->v));
        
        memset(fluid->du, 0, sizeof(fluid->du));
        memset(fluid->dv, 0, sizeof(fluid->dv));
        memset(fluid->u, 0, sizeof(fluid->u));
        memset(fluid->v, 0, sizeof(fluid->v));
        
        for (int i = 0; i < (int)fluid->fNumCells; i++) {
            fluid->cellType[i] = (fluid->s[i] == 0.0f) ? SOLID_CELL : AIR_CELL;
        }
        
        for (int i = 0; i < fluid->numParticles; i++) {
            float x = fluid->particlePos[2 * i];
            float y = fluid->particlePos[2 * i + 1];
            int xi = clampi((int)floorf(x * h1), 1, (int)fluid->fNumX - 2);
            int yi = clampi((int)floorf(y * h1), 1, (int)fluid->fNumY - 2);
            int cellNr = xi * (int)n + yi;
            
            if (fluid->cellType[cellNr] == AIR_CELL) {
                fluid->cellType[cellNr] = FLUID_CELL;
            }
        }
    }
    
    for (int component = 0; component < 2; component++) {
        float dx = (component == 0) ? 0.0f : h2;
        float dy = (component == 0) ? h2 : 0.0f;
        
        float* f = (component == 0) ? fluid->u : fluid->v;
        float* prevF = (component == 0) ? fluid->prevU : fluid->prevV;
        float* d = (component == 0) ? fluid->du : fluid->dv;
        
        for (int i = 0; i < fluid->numParticles; i++) {
            float x = fluid->particlePos[2 * i];
            float y = fluid->particlePos[2 * i + 1];
            
            x = clampf(x, h, (fluid->fNumX - 1.0f) * h);
            y = clampf(y, h, (fluid->fNumY - 1.0f) * h);
            
            float x0 = fminf(floorf((x - dx) * h1), fluid->fNumX - 2.0f);
            float tx = ((x - dx) - x0 * h) * h1;
            float x1 = fminf(x0 + 1.0f, fluid->fNumX - 2.0f);
            
            float y0 = fminf(floorf((y - dy) * h1), fluid->fNumY - 2.0f);
            float ty = ((y - dy) - y0 * h) * h1;
            float y1 = fminf(y0 + 1.0f, fluid->fNumY - 2.0f);
            
            float sx = 1.0f - tx;
            float sy = 1.0f - ty;
            
            float d0 = sx * sy;
            float d1 = tx * sy;
            float d2 = tx * ty;
            float d3 = sx * ty;
            
            int nr0 = (int)(x0 * n + y0);
            int nr1 = (int)(x1 * n + y0);
            int nr2 = (int)(x1 * n + y1);
            int nr3 = (int)(x0 * n + y1);
            
            if (toGrid) {
                float pv = fluid->particleVel[2 * i + component];
                f[nr0] += pv * d0; d[nr0] += d0;
                f[nr1] += pv * d1; d[nr1] += d1;
                f[nr2] += pv * d2; d[nr2] += d2;
                f[nr3] += pv * d3; d[nr3] += d3;
            } else {
                float offset = (component == 0) ? n : 1.0f;
                float valid0 = (fluid->cellType[nr0] != AIR_CELL || fluid->cellType[(int)(nr0 - offset)] != AIR_CELL) ? 1.0f : 0.0f;
                float valid1 = (fluid->cellType[nr1] != AIR_CELL || fluid->cellType[(int)(nr1 - offset)] != AIR_CELL) ? 1.0f : 0.0f;
                float valid2 = (fluid->cellType[nr2] != AIR_CELL || fluid->cellType[(int)(nr2 - offset)] != AIR_CELL) ? 1.0f : 0.0f;
                float valid3 = (fluid->cellType[nr3] != AIR_CELL || fluid->cellType[(int)(nr3 - offset)] != AIR_CELL) ? 1.0f : 0.0f;
                
                float v = fluid->particleVel[2 * i + component];
                float denom = valid0 * d0 + valid1 * d1 + valid2 * d2 + valid3 * d3;
                
                if (denom > 0.0f) {
                    float picV = (valid0 * d0 * f[nr0] + valid1 * d1 * f[nr1] + valid2 * d2 * f[nr2] + valid3 * d3 * f[nr3]) / denom;
                    float corr = (valid0 * d0 * (f[nr0] - prevF[nr0]) + valid1 * d1 * (f[nr1] - prevF[nr1]) +
                                 valid2 * d2 * (f[nr2] - prevF[nr2]) + valid3 * d3 * (f[nr3] - prevF[nr3])) / denom;
                    float flipV = v + corr;
                    
                    fluid->particleVel[2 * i + component] = (1.0f - flipRatio) * picV + flipRatio * flipV;
                }
            }
        }
        
        if (toGrid) {
            for (int i = 0; i < NUM_CELLS_X2; i++) {
                if (d[i] > 0.0f) {
                    f[i] /= d[i];
                }
            }
            
            for (int i = 0; i < (int)fluid->fNumX; i++) {
                for (int j = 0; j < (int)fluid->fNumY; j++) {
                    bool solid = fluid->cellType[i * (int)n + j] == SOLID_CELL;
                    if (solid || (i > 0 && fluid->cellType[(i - 1) * (int)n + j] == SOLID_CELL)) {
                        fluid->u[i * (int)n + j] = fluid->prevU[i * (int)n + j];
                    }
                    if (solid || (j > 0 && fluid->cellType[i * (int)n + j - 1] == SOLID_CELL)) {
                        fluid->v[i * (int)n + j] = fluid->prevV[i * (int)n + j];
                    }
                }
            }
        }
    }
}

void solveIncompressibility(FlipFluid* fluid, int numIters, float dt, float overRelaxation, bool compensateDrift) {
    memset(fluid->p, 0, sizeof(fluid->p));
    memcpy(fluid->prevU, fluid->u, sizeof(fluid->u));
    memcpy(fluid->prevV, fluid->v, sizeof(fluid->v));
    
    float n = fluid->fNumY;
    float cp = fluid->density * fluid->h / dt;
    
    for (int iter = 0; iter < numIters; iter++) {
        for (int i = 1; i < (int)fluid->fNumX - 1; i++) {
            for (int j = 1; j < (int)fluid->fNumY - 1; j++) {
                if (fluid->cellType[i * (int)n + j] != FLUID_CELL) continue;
                
                int center = i * (int)n + j;
                int left = (i - 1) * (int)n + j;
                int right = (i + 1) * (int)n + j;
                int bottom = i * (int)n + j - 1;
                int top = i * (int)n + j + 1;
                
                float sx0 = fluid->s[left];
                float sx1 = fluid->s[right];
                float sy0 = fluid->s[bottom];
                float sy1 = fluid->s[top];
                float s = sx0 + sx1 + sy0 + sy1;
                
                if (s == 0.0f) continue;
                
                float div = fluid->u[right] - fluid->u[center] + fluid->v[top] - fluid->v[center];
                
                if (fluid->particleRestDensity > 0.0f && compensateDrift) {
                    float k = 1.0f;
                    float compression = fluid->particleDensity[i * (int)n + j] - fluid->particleRestDensity;
                    if (compression > 0.0f) {
                        div = div - k * compression;
                    }
                }
                
                float p = -div / s;
                p *= overRelaxation;
                fluid->p[center] += cp * p;
                
                fluid->u[center] -= sx0 * p;
                fluid->u[right] += sx1 * p;
                fluid->v[center] -= sy0 * p;
                fluid->v[top] += sy1 * p;
            }
        }
    }
}

void simulate(FlipFluid* fluid, float dt, float xGravity, float yGravity, float flipRatio,
              int numPressureIters, int numParticleIters, float overRelaxation,
              bool compensateDrift, bool separateParticles) {
    int numSubSteps = 1;
    float sdt = dt / (float)numSubSteps;
    
    for (int step = 0; step < numSubSteps; step++) {
        integrateParticles(fluid, sdt, yGravity, xGravity);
        pushParticlesApart(fluid, numParticleIters);
        handleParticleCollisions(fluid);
        pushParticlesApart(fluid, numParticleIters);
        handleParticleCollisions(fluid);
        transferVelocities(fluid, true, 1.9f);
        updateParticleDensity(fluid);
        solveIncompressibility(fluid, numPressureIters, sdt, overRelaxation, compensateDrift);
        transferVelocities(fluid, false, flipRatio);
        showParticles(fluid, true); // デフォルトでスムースモードを使用
    }
}

// Scene functions
void initScene(Scene* scene, int particles) {
    scene->xGravity = 0.0f;
    scene->yGravity = 9.81f;  // 画面座標系では正の値が下向き
    scene->dt = 1.0f / 60.0f;
    scene->flipRatio = 0.85f;
    scene->numPressureIters = 10;
    scene->numParticleIters = 1;
    scene->frameNr = 0;
    scene->overRelaxation = 1.9f;
    scene->compensateDrift = true;
    scene->separateParticles = true;
    scene->paused = false;
    scene->smoothMode = true;  // デフォルトはスムースモード
    
    float res = 23.0f;
    float tankHeight = 1.0f * SIM_HEIGHT;
    float tankWidth = 1.0f * SIM_WIDTH;
    float h = tankHeight / res;
    float density = 1000.0f;
    float r = 0.5f * h;
    
    initFlipFluid(&scene->fluid, density, tankWidth, tankHeight, h, r, MAX_PARTICLES);
    scene->fluid.numParticles = particles;
}

void simulateScene(Scene* scene) {
    // Clear all non-solid cells before simulation
    for (int i = 0; i < NUM_CELLS; i++) {
        if (scene->fluid.cellType[i] != SOLID_CELL) {
            scene->fluid.cellType[i] = AIR_CELL;
        }
    }
    
    simulate(&scene->fluid, scene->dt, scene->xGravity, scene->yGravity, scene->flipRatio,
             scene->numPressureIters, scene->numParticleIters, scene->overRelaxation,
             scene->compensateDrift, scene->separateParticles);
    
    scene->frameNr++;
}

// SDL2 Visualization
void drawGrid(SDL_Renderer* renderer, Scene* scene) {
    SDL_SetRenderDrawColor(renderer, 64, 64, 64, 255);
    
    for (int i = 0; i <= 21; i++) {
        int x = 50 + i * GRID_SIZE;
        SDL_RenderDrawLine(renderer, x, 50, x, 50 + 21 * GRID_SIZE);
    }
    
    for (int j = 0; j <= 21; j++) {
        int y = 50 + j * GRID_SIZE;
        SDL_RenderDrawLine(renderer, 50, y, 50 + 21 * GRID_SIZE, y);
    }
}

void drawFluid(SDL_Renderer* renderer, Scene* scene) {
    if (scene->smoothMode) {
        // スムースモード: 密度ベースの色付け
        int particleCount[NUM_CELLS] = {0};
        
        for (int i = 0; i < scene->fluid.numParticles; i++) {
            float x = scene->fluid.particlePos[2 * i];
            float y = scene->fluid.particlePos[2 * i + 1];
            
            int cell_x = (int)floorf(x);
            int cell_y = (int)floorf(y);
            
            if (cell_x >= 0 && cell_x < 23 && cell_y >= 0 && cell_y < 23) {
                int cellIndex = cell_y * 23 + cell_x;
                if (cellIndex >= 0 && cellIndex < NUM_CELLS) {
                    particleCount[cellIndex]++;
                }
            }
        }
        
        for (int i = 1; i < 22; i++) {
            for (int j = 1; j < 22; j++) {
                if (scene->fluid.cellType[i * 23 + j] == FLUID_CELL) {
                    // 密度に応じて色を調整
                    int density = particleCount[i * 23 + j];
                    int blue = 255;
                    int green = 150 - (density * 20);
                    int red = density * 30;
                    
                    green = (green < 50) ? 50 : (green > 150) ? 150 : green;
                    red = (red > 100) ? 100 : red;
                    
                    SDL_SetRenderDrawColor(renderer, red, green, blue, 255);
                    
                    SDL_Rect rect = {
                        50 + (j - 1) * GRID_SIZE + 1,
                        50 + (i - 1) * GRID_SIZE + 1,
                        GRID_SIZE - 2,
                        GRID_SIZE - 2
                    };
                    SDL_RenderFillRect(renderer, &rect);
                }
            }
        }
    } else {
        // クラシックモード: 単色
        SDL_SetRenderDrawColor(renderer, 0, 150, 255, 255);
        
        for (int i = 1; i < 22; i++) {
            for (int j = 1; j < 22; j++) {
                if (scene->fluid.cellType[i * 23 + j] == FLUID_CELL) {
                    SDL_Rect rect = {
                        50 + (j - 1) * GRID_SIZE + 1,
                        50 + (i - 1) * GRID_SIZE + 1,
                        GRID_SIZE - 2,
                        GRID_SIZE - 2
                    };
                    SDL_RenderFillRect(renderer, &rect);
                }
            }
        }
    }
}

void drawParticles(SDL_Renderer* renderer, Scene* scene) {
    if (scene->smoothMode) {
        // スムースモード: 半透明で少し大きめ
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 180);
        
        for (int i = 0; i < scene->fluid.numParticles; i++) {
            float x = scene->fluid.particlePos[2 * i];
            float y = scene->fluid.particlePos[2 * i + 1];
            
            int screenX = 50 + (int)(x * GRID_SIZE);
            int screenY = 50 + (int)(y * GRID_SIZE);
            
            SDL_Rect rect = {screenX - 2, screenY - 2, 4, 4};
            SDL_RenderFillRect(renderer, &rect);
        }
    } else {
        // クラシックモード: 小さくて明確
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        
        for (int i = 0; i < scene->fluid.numParticles; i++) {
            float x = scene->fluid.particlePos[2 * i];
            float y = scene->fluid.particlePos[2 * i + 1];
            
            int screenX = 50 + (int)(x * GRID_SIZE);
            int screenY = 50 + (int)(y * GRID_SIZE);
            
            SDL_Rect rect = {screenX - 1, screenY - 1, 2, 2};
            SDL_RenderFillRect(renderer, &rect);
        }
    }
}

void drawInfo(SDL_Renderer* renderer, Scene* scene) {
    // This is a simple info display - in a real application you'd use a font rendering library
    // For now, we'll just display some basic statistics with simple rectangles
    
    // Draw a simple info panel background
    SDL_SetRenderDrawColor(renderer, 32, 32, 32, 200);
    SDL_Rect infoPanel = {500, 50, 250, 250};
    SDL_RenderFillRect(renderer, &infoPanel);
    
    // Draw border
    SDL_SetRenderDrawColor(renderer, 128, 128, 128, 255);
    SDL_RenderDrawRect(renderer, &infoPanel);
    
    // Simple particle count visualization (each dot represents 10 particles)
    SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
    int particleGroups = scene->fluid.numParticles / 10;
    for (int i = 0; i < particleGroups && i < 100; i++) {
        int x = 510 + (i % 20) * 4;
        int y = 80 + (i / 20) * 4;
        SDL_Rect dot = {x, y, 2, 2};
        SDL_RenderFillRect(renderer, &dot);
    }
    
    // Draw gravity direction indicator
    SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255);
    int centerX = 625;
    int centerY = 180;
    int arrowSize = 20;
    
    // Draw center point
    SDL_Rect centerPoint = {centerX - 2, centerY - 2, 4, 4};
    SDL_RenderFillRect(renderer, &centerPoint);
    
    // Draw gravity arrow based on current gravity direction
    // 画面座標系に合わせて矢印を表示
    if (scene->yGravity > 0) {
        // Downward gravity (positive Y is down in screen coordinates)
        SDL_RenderDrawLine(renderer, centerX, centerY, centerX, centerY + arrowSize);
        SDL_RenderDrawLine(renderer, centerX, centerY + arrowSize, centerX - 5, centerY + arrowSize - 5);
        SDL_RenderDrawLine(renderer, centerX, centerY + arrowSize, centerX + 5, centerY + arrowSize - 5);
    } else if (scene->yGravity < 0) {
        // Upward gravity (negative Y is up in screen coordinates)
        SDL_RenderDrawLine(renderer, centerX, centerY, centerX, centerY - arrowSize);
        SDL_RenderDrawLine(renderer, centerX, centerY - arrowSize, centerX - 5, centerY - arrowSize + 5);
        SDL_RenderDrawLine(renderer, centerX, centerY - arrowSize, centerX + 5, centerY - arrowSize + 5);
    }
    
    if (scene->xGravity < 0) {
        // Leftward gravity
        SDL_RenderDrawLine(renderer, centerX, centerY, centerX - arrowSize, centerY);
        SDL_RenderDrawLine(renderer, centerX - arrowSize, centerY, centerX - arrowSize + 5, centerY - 5);
        SDL_RenderDrawLine(renderer, centerX - arrowSize, centerY, centerX - arrowSize + 5, centerY + 5);
    } else if (scene->xGravity > 0) {
        // Rightward gravity
        SDL_RenderDrawLine(renderer, centerX, centerY, centerX + arrowSize, centerY);
        SDL_RenderDrawLine(renderer, centerX + arrowSize, centerY, centerX + arrowSize - 5, centerY - 5);
        SDL_RenderDrawLine(renderer, centerX + arrowSize, centerY, centerX + arrowSize - 5, centerY + 5);
    }
    
    if (scene->xGravity == 0.0f && scene->yGravity == 0.0f) {
        // No gravity - draw X
        SDL_RenderDrawLine(renderer, centerX - 10, centerY - 10, centerX + 10, centerY + 10);
        SDL_RenderDrawLine(renderer, centerX - 10, centerY + 10, centerX + 10, centerY - 10);
    }
}

void handleInput(SDL_Event* e, Scene* scene, bool* running) {
    if (e->type == SDL_QUIT) {
        *running = false;
    } else if (e->type == SDL_KEYDOWN) {
        switch (e->key.keysym.sym) {
            case SDLK_ESCAPE:
                *running = false;
                break;
            case SDLK_SPACE:
                scene->paused = !scene->paused;
                break;
            case SDLK_r:
                // Reset simulation
                initScene(scene, 200);
                break;
            case SDLK_UP:
                // Add particles
                if (scene->fluid.numParticles < MAX_PARTICLES - 10) {
                    scene->fluid.numParticles += 10;
                }
                break;
            case SDLK_DOWN:
                // Remove particles
                if (scene->fluid.numParticles > 10) {
                    scene->fluid.numParticles -= 10;
                }
                break;
            case SDLK_g:
                // Toggle gravity (down or off)
                if (scene->yGravity == 0.0f && scene->xGravity == 0.0f) {
                    scene->yGravity = 9.81f;  // 下向き（正の値）
                    scene->xGravity = 0.0f;
                } else {
                    scene->yGravity = 0.0f;
                    scene->xGravity = 0.0f;
                }
                break;
            case SDLK_LEFT:
                // Gravity to the left
                scene->xGravity = -9.81f;
                scene->yGravity = 0.0f;
                break;
            case SDLK_RIGHT:
                // Gravity to the right
                scene->xGravity = 9.81f;
                scene->yGravity = 0.0f;
                break;
            case SDLK_u:
                // Gravity upward
                scene->xGravity = 0.0f;
                scene->yGravity = -9.81f;  // 上向き（負の値）
                break;
            case SDLK_d:
                // Gravity downward
                scene->xGravity = 0.0f;
                scene->yGravity = 9.81f;   // 下向き（正の値）
                break;
            case SDLK_m:
                // Toggle display mode
                scene->smoothMode = !scene->smoothMode;
                printf("Display mode: %s\n", scene->smoothMode ? "Smooth" : "Classic");
                break;
	}
    } else if (e->type == SDL_MOUSEBUTTONDOWN) {
        if (e->button.button == SDL_BUTTON_LEFT) {
            // Add particles at mouse position
            int mouseX, mouseY;
            SDL_GetMouseState(&mouseX, &mouseY);
            
            // Convert screen coordinates to simulation coordinates
            float simX = (float)(mouseX - 50) / GRID_SIZE;
            float simY = (float)(mouseY - 50) / GRID_SIZE;
            
            // Add a few particles around the mouse position
            for (int i = 0; i < 5 && scene->fluid.numParticles < MAX_PARTICLES; i++) {
                int idx = scene->fluid.numParticles;
                scene->fluid.particlePos[2 * idx] = simX + (rand() % 100 - 50) / 100.0f;
                scene->fluid.particlePos[2 * idx + 1] = simY + (rand() % 100 - 50) / 100.0f;
                scene->fluid.particleVel[2 * idx] = 0.0f;
                scene->fluid.particleVel[2 * idx + 1] = 0.0f;
                scene->fluid.numParticles++;
            }
        }
    }
}

int main(int argc, char* argv[]) {
    // Initialize SDL
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
        return 1;
    }
    
    // Create window
    SDL_Window* window = SDL_CreateWindow("Fluid Simulation",
                                        SDL_WINDOWPOS_UNDEFINED,
                                        SDL_WINDOWPOS_UNDEFINED,
                                        WINDOW_WIDTH, WINDOW_HEIGHT,
                                        SDL_WINDOW_SHOWN);
    if (window == NULL) {
        printf("Window could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    
    // Create renderer
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == NULL) {
        printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    // Initialize scene
    Scene scene;
    initScene(&scene, 200);
    
    // Initialize grid boundaries
    float n = scene.fluid.fNumY;
    for (int i = 0; i < (int)scene.fluid.fNumX; i++) {
        for (int j = 0; j < (int)scene.fluid.fNumY; j++) {
            float s = 1.0f; // fluid
            if (i == 0 || i == (int)scene.fluid.fNumX - 1 || j == 0) {
                s = 0.0f; // solid
            }
            scene.fluid.s[i * (int)n + j] = s;
        }
    }
    
    // Main loop
    bool running = true;
    SDL_Event e;
    Uint32 lastTime = SDL_GetTicks();
    
    printf("Fluid Simulation Controls:\n");
    printf("SPACE - Pause/Resume\n");
    printf("R - Reset simulation\n");
    printf("UP/DOWN - Add/Remove particles\n");
    printf("LEFT/RIGHT - Set gravity direction (left/right)\n");
    printf("U/D - Set gravity direction (up/down)\n");
    printf("G - Toggle gravity (down/off)\n");
    printf("M - Toggle display mode (Smooth/Classic)\n");
    printf("Left Click - Add particles at mouse position\n");
    printf("ESC - Exit\n\n");
    
    while (running) {
        Uint32 currentTime = SDL_GetTicks();
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        lastTime = currentTime;
        
        // Handle events
        while (SDL_PollEvent(&e)) {
            handleInput(&e, &scene, &running);
        }
        
        // Debug: Print gravity values when they change
        static float lastXGrav = 0, lastYGrav = 0;
        static int debugCounter = 0;
        if (scene.xGravity != lastXGrav || scene.yGravity != lastYGrav) {
            printf("Gravity: X=%.2f, Y=%.2f\n", scene.xGravity, scene.yGravity);
            lastXGrav = scene.xGravity;
            lastYGrav = scene.yGravity;
        }
        
        // Debug: Print some particle positions occasionally
        debugCounter++;
        if (debugCounter % 60 == 0 && scene.fluid.numParticles > 0) {
            printf("Sample particle positions:\n");
            for (int i = 0; i < 3 && i < scene.fluid.numParticles; i++) {
                printf("  Particle %d: (%.2f, %.2f)\n", i, 
                       scene.fluid.particlePos[2*i], scene.fluid.particlePos[2*i+1]);
            }
        }
        
        // Update simulation
        if (!scene.paused) {
            simulateScene(&scene);
        }
        
        // Clear screen
        SDL_SetRenderDrawColor(renderer, 20, 20, 20, 255);
        SDL_RenderClear(renderer);
        
        // Draw simulation
        drawGrid(renderer, &scene);
        drawFluid(renderer, &scene);
        drawParticles(renderer, &scene);
        drawInfo(renderer, &scene);
        
    // Draw mode indicator
    if (scene.smoothMode) {
        SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
        SDL_Rect smoothIndicator = {10, 40, 15, 15};
        SDL_RenderFillRect(renderer, &smoothIndicator);
    } else {
        SDL_SetRenderDrawColor(renderer, 255, 0, 255, 255);
        SDL_Rect classicIndicator = {10, 40, 15, 15};
        SDL_RenderFillRect(renderer, &classicIndicator);
    }
        
        // Present the frame
        SDL_RenderPresent(renderer);
        
        // Cap the frame rate to ~60 FPS
        SDL_Delay(16);
    }
    
    // Clean up
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    
    return 0;
}
