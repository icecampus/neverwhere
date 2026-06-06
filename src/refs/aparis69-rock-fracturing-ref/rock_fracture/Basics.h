#pragma once

#include "Vec.h"

#include <cstdlib>
#include <vector>

namespace rock_fracture {

class Random {
public:
    Random() {}

    static inline double Uniform(double a, double b) {
        return a + (b - a) * Uniform();
    }

    static inline double Uniform() {
        return double(std::rand()) / double(RAND_MAX);
    }

    static inline int Integer() {
        return std::rand();
    }
};


class Ray {
public:
    Vector3 o;
    Vector3 d;

    Ray() {}
    Ray(const Vector3& oo, const Vector3& dd) : o(oo), d(dd) {}
    Vector3 operator()(double t) const { return o + d * t; }
};


class Plane {
protected:
    Vector3 p;
    Vector3 n;
    double c;

public:
    Plane() : p(0.0), n(0.0), c(0.0) {}
    Plane(const Vector3& p, const Vector3& n);

    Vector3 Normal() const { return n; }
    Vector3 Point() const { return p; }
    int Side(const Vector3& p) const;
    double Signed(const Vector3& p) const;
    static bool Intersection(const Plane& a, const Plane& b, const Plane& c, Vector3& p);
    static std::vector<Vector3> ConvexPoints(const std::vector<Plane>& planes);
};

inline Plane::Plane(const Vector3& pp, const Vector3& nn) {
    p = pp;
    n = nn;
    c = Dot(p, n);
}

inline double Plane::Signed(const Vector3& pp) const {
    return Dot(n, pp) - c;
}

inline int Plane::Side(const Vector3& pp) const {
    double r = Dot(n, pp) - c;
    if (r > 1e-6) return 1;
    if (r < -1e-6) return -1;
    return 0;
}

inline bool Plane::Intersection(const Plane& a, const Plane& b, const Plane& c, Vector3& p) {
    double e = Matrix4(Matrix3(a.Normal(), b.Normal(), c.Normal())).Determinant();
    if (e < 1e-6) return false;
    p = (Dot(a.Point(), a.Normal()) * (Cross(b.Normal(), c.Normal()))) +
        (Dot(b.Point(), b.Normal()) * (Cross(c.Normal(), a.Normal()))) +
        (Dot(c.Point(), c.Normal()) * (Cross(a.Normal(), b.Normal())));
    p = p / (-e);
    return true;
}

inline std::vector<Vector3> Plane::ConvexPoints(const std::vector<Plane>& planes) {
    std::vector<Vector3> pts;
    for (int i = 0; i < (int)planes.size(); i++) {
        for (int j = i + 1; j < (int)planes.size(); j++) {
            for (int k = j + 1; k < (int)planes.size(); k++) {
                Vector3 p;
                bool intersect = Intersection(planes[i], planes[j], planes[k], p);
                if (intersect) {
                    bool isInside = true;
                    for (int l = 0; l < (int)planes.size(); l++) {
                        if (l == i || l == j || l == k) continue;
                        int s = planes[l].Side(p);
                        if (s > 0) {
                            isInside = false;
                            break;
                        }
                    }
                    if (isInside) pts.push_back(p);
                }
            }
        }
    }
    return pts;
}


class Triangle {
private:
    Vector3 pts[3];

public:
    Triangle() {}
    Triangle(const Vector3& a, const Vector3& b, const Vector3& c) {
        pts[0] = a; pts[1] = b; pts[2] = c;
    }
    Vector3 Center() const { return (pts[0] + pts[1] + pts[2]) / 3.0; }
    Vector3 Normal() const { return Normalize(Cross(pts[1] - pts[0], pts[2] - pts[0])); }
    Vector3 Point(int i) const { return pts[i]; }
};


class Box {
protected:
    Vector3 a;
    Vector3 b;

public:
    inline Box() {}
    explicit Box(const Vector3& A, const Vector3& B);
    explicit Box(const Vector3& C, double R);
    explicit Box(const Box& b1, const Box& b2);
    explicit Box(const std::vector<Vector3>& pts);

    bool Contains(const Vector3&) const;
    Box Extended(const Vector3&) const;
    Vector3 Center() const;
    double Distance(const Vector3& p) const;
    Vector3 Diagonal() const;
    Vector3 Size() const;
    Vector3 RandomInside() const;
    void SetParallelepipedic(double size, int& x, int& y, int& z);
    void SetParallelepipedic(int n, int& x, int& y, int& z);
    Vector3 Vertex(int) const;
    Vector3 BottomLeft() const;
    Vector3 TopRight() const;
    Vector3& operator[](int i);
    Vector3 operator[](int i) const;
};

inline Box::Box(const Vector3& A, const Vector3& B) : a(A), b(B) {}

inline Box::Box(const Vector3& C, double R) {
    Vector3 RR = Vector3(R);
    a = C - RR;
    b = C + RR;
}

inline Box::Box(const Box& b1, const Box& b2) {
    a = Vector3::Min(b1.a, b2.a);
    b = Vector3::Max(b1.b, b2.b);
}

inline Box::Box(const std::vector<Vector3>& pts) {
    for (int j = 0; j < 3; j++) {
        a[j] = pts.at(0)[j];
        b[j] = pts.at(0)[j];
        for (int i = 1; i < (int)pts.size(); i++) {
            if (pts.at(i)[j] < a[j]) a[j] = pts.at(i)[j];
            if (pts.at(i)[j] > b[j]) b[j] = pts.at(i)[j];
        }
    }
}

inline bool Box::Contains(const Vector3& p) const { return (p > a && p < b); }
inline Box Box::Extended(const Vector3& r) const { return Box(a - r, b + r); }
inline Vector3 Box::Center() const { return (a + b) / 2.0; }
inline Vector3 Box::Diagonal() const { return (b - a); }
inline Vector3 Box::Size() const { return (b - a); }

inline void Box::SetParallelepipedic(double size, int& x, int& y, int& z) {
    Vector3 d = (b - a);
    x = int(d[0] / size);
    y = int(d[1] / size);
    z = int(d[2] / size);
    if (x == 0) x++;
    if (y == 0) y++;
    if (z == 0) z++;
    Vector3 c = (a + b) * 0.5;
    Vector3 e = Vector3(double(x), double(y), double(z)) * size / 2.0;
    a = c - e;
    b = c + e;
}

inline void Box::SetParallelepipedic(int n, int& x, int& y, int& z) {
    Vector3 d = (b - a);
    double e = d.Max();
    double size = e / n;
    SetParallelepipedic(size, x, y, z);
}

inline double Box::Distance(const Vector3& p) const {
    double r = 0.0;
    for (int i = 0; i < 3; i++) {
        if (p[i] < a[i]) {
            double s = p[i] - a[i];
            r += s * s;
        } else if (p[i] > b[i]) {
            double s = p[i] - b[i];
            r += s * s;
        }
    }
    return r;
}

inline Vector3 Box::RandomInside() const {
    Vector3 s = b - a;
    double randw = Random::Uniform(-1.0 * s[0] / 2.0, s[0] / 2.0);
    double randh = Random::Uniform(-1.0 * s[1] / 2.0, s[1] / 2.0);
    double randl = Random::Uniform(-1.0 * s[2] / 2.0, s[2] / 2.0);
    return (a + b) / 2.0 + Vector3(randw, randh, randl);
}

inline Vector3 Box::Vertex(int i) const { if (i == 0) return a; return b; }
inline Vector3 Box::BottomLeft() const { return a; }
inline Vector3 Box::TopRight() const { return b; }
inline Vector3& Box::operator[](int i) { if (i == 0) return a; return b; }
inline Vector3 Box::operator[](int i) const { if (i == 0) return a; return b; }


class Box2D {
protected:
    Vector2 a;
    Vector2 b;

public:
    explicit Box2D() : a(0.0), b(0.0) {}
    explicit Box2D(const Vector2& A, const Vector2& B) : a(A), b(B) {}
    explicit Box2D(const Vector2& C, double R) {
        Vector2 RR = Vector2(R);
        a = C - RR;
        b = C + RR;
    }
    explicit Box2D(const Box& box) {
        a = Vector2(box.Vertex(0));
        b = Vector2(box.Vertex(1));
    }

    bool Contains(const Vector2&) const;
    bool Intersect(const Box2D& box) const;
    double Distance(const Vector2& p) const;
    Vector2 Vertex(int i) const;
    Vector2 Center() const;
    Vector2 BottomLeft() const;
    Vector2 TopRight() const;
    Box ToBox(double zMin, double zMax) const;
    Vector2& operator[](int i);
    Vector2 operator[](int i) const;
};

inline bool Box2D::Contains(const Vector2& p) const { return (p > a && p < b); }

inline bool Box2D::Intersect(const Box2D& box) const {
    if (((a[0] >= box.b[0]) || (a[1] >= box.b[1]) || (b[0] <= box.a[0]) || (b[1] <= box.a[1])))
        return false;
    return true;
}

inline double Box2D::Distance(const Vector2& p) const {
    double r = 0.0;
    for (int i = 0; i < 2; i++) {
        if (p[i] < a[i]) {
            double s = p[i] - a[i];
            r += s * s;
        } else if (p[i] > b[i]) {
            double s = p[i] - b[i];
            r += s * s;
        }
    }
    return r;
}

inline Vector2 Box2D::Vertex(int i) const { if (i == 0) return a; return b; }
inline Vector2 Box2D::Center() const { return (a + b) / 2.0; }
inline Vector2 Box2D::BottomLeft() const { return a; }
inline Vector2 Box2D::TopRight() const { return b; }
inline Box Box2D::ToBox(double zMin, double zMax) const {
    return Box(a.ToVector3(zMin), b.ToVector3(zMax));
}
inline Vector2& Box2D::operator[](int i) { if (i == 0) return a; return b; }
inline Vector2 Box2D::operator[](int i) const { if (i == 0) return a; return b; }


class Circle {
protected:
    Vector3 center;
    Vector3 normal;
    double radius;

public:
    Circle(const Vector3& c, const Vector3& n, double r) : center(c), normal(n), radius(r) {}

    Vector3 Center() const { return center; }
    Vector3 Normal() const { return normal; }
    double Radius() const { return radius; }
    bool Intersect(const Ray& r, double& t) const;
};

inline bool Circle::Intersect(const Ray& ray, double& t) const {
    double e = Dot(normal, ray.d);
    if (std::fabs(e) < 1e-6) return false;
    t = Dot(center - ray.o, normal) / e;
    if (t < 0.0) return false;
    Vector3 p = ray(t) - center;
    if (Dot(p, p) > radius * radius) return false;
    return true;
}


class Sphere {
protected:
    Vector3 center;
    double radius;

public:
    Sphere(const Vector3& c, double r) : center(c), radius(r) {}

    double Distance(const Vector3& p) const;
    Vector3 RandomSurface() const;
    Vector3 Center() const { return center; }
    double Radius() const { return radius; }
};

inline double Sphere::Distance(const Vector3& p) const {
    double a = Dot((p - center), (p - center));
    if (a < radius * radius) return 0.0;
    a = std::sqrt(a) - radius;
    a *= a;
    return a;
}

inline Vector3 Sphere::RandomSurface() const {
    return Vector3(0);
}


class ScalarField2D {
protected:
    Box2D box;
    int nx, ny;
    std::vector<double> values;

public:
    inline ScalarField2D() : nx(0), ny(0) {}
    inline ScalarField2D(int nx, int ny, const Box2D& bbox) : box(bbox), nx(nx), ny(ny) {
        values.resize(size_t(nx * ny));
    }
    inline ScalarField2D(int nx, int ny, const Box2D& bbox, double value) : box(bbox), nx(nx), ny(ny) {
        values.resize(nx * ny);
        Fill(value);
    }
    inline ScalarField2D(int nx, int ny, const Box2D& bbox, const std::vector<double>& vals)
        : ScalarField2D(nx, ny, bbox) {
        for (size_t i = 0; i < vals.size(); i++) values[i] = vals[i];
    }
    inline ScalarField2D(const ScalarField2D& field) : ScalarField2D(field.nx, field.ny, field.box) {
        for (size_t i = 0; i < values.size(); i++) values[i] = field.values[i];
    }
    inline ~ScalarField2D() {}

    inline Vector3 Vertex(int i, int j) const {
        double x = box.Vertex(0).x + i * (box.Vertex(1).x - box.Vertex(0).x) / (nx - 1);
        double y = Get(i, j);
        double z = box.Vertex(0).y + j * (box.Vertex(1).y - box.Vertex(0).y) / (ny - 1);
        return Vector3(z, y, x);
    }

    inline Vector3 Vertex(const Vector2& v) const {
        return Vector3(v.x, GetValueBilinear(v), v.y);
    }

    inline bool Inside(const Vector2& p) const {
        Vector2 q = p - box.Vertex(0);
        Vector2 d = box.Vertex(1) - box.Vertex(0);
        double u = q[0] / d[0];
        double v = q[1] / d[1];
        int j = int(u * (nx - 1));
        int i = int(v * (ny - 1));
        return Inside(i, j);
    }

    inline bool Inside(int i, int j) const {
        if (i < 0 || i >= nx || j < 0 || j >= ny) return false;
        return true;
    }

    inline int ToIndex1D(int i, int j) const { return i * nx + j; }

    inline double Get(int row, int column) const {
        int index = ToIndex1D(row, column);
        return values[index];
    }

    inline double GetValueBilinear(const Vector2& p) const {
        Vector2 q = p - box.Vertex(0);
        Vector2 d = box.Vertex(1) - box.Vertex(0);
        double texelX = 1.0 / double(nx - 1);
        double texelY = 1.0 / double(ny - 1);
        double u = q[0] / d[0];
        double v = q[1] / d[1];
        int i = int(v * (ny - 1));
        int j = int(u * (nx - 1));
        if (!Inside(i, j) || !Inside(i + 1, j + 1)) return -1.0;
        double anchorU = j * texelX;
        double anchorV = i * texelY;
        double localU = (u - anchorU) / texelX;
        double localV = (v - anchorV) / texelY;
        double v1 = Get(i, j);
        double v2 = Get(i + 1, j);
        double v3 = Get(i + 1, j + 1);
        double v4 = Get(i, j + 1);
        return (1 - localU) * (1 - localV) * v1
             + (1 - localU) * localV * v2
             + localU * (1 - localV) * v4
             + localU * localV * v3;
    }

    inline void Fill(double v) { std::fill(values.begin(), values.end(), v); }

    inline void Set(int row, int column, double v) {
        values[ToIndex1D(row, column)] = v;
    }

    inline double Max() const {
        if (values.empty()) return 0.0;
        double m = values[0];
        for (size_t i = 1; i < values.size(); i++) if (values[i] > m) m = values[i];
        return m;
    }

    inline double Min() const {
        if (values.empty()) return 0.0;
        double m = values[0];
        for (size_t i = 1; i < values.size(); i++) if (values[i] < m) m = values[i];
        return m;
    }

    inline Box2D GetBox() const { return box; }
};

} // namespace rock_fracture
