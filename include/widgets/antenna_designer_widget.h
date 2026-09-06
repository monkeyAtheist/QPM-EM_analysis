#pragma once

#include "antenna_surface_geometry.h"

#include <QPointF>
#include <QString>
#include <QWidget>
#include <QVector>

#include <set>
#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class FieldProfilePlot;

namespace HybridWireSurfaceMom { struct Input; struct Result; }
namespace EmCalculator { enum class AntennaKind; struct AntennaInputs; }

class AntennaDesignerWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit AntennaDesignerWidget(QWidget *parent = nullptr);
    void setShowPhysicalWireRadius(bool enabled);
    bool showPhysicalWireRadius() const { return m_showPhysicalWireRadius; }
    bool createFromCalculator(EmCalculator::AntennaKind kind, const EmCalculator::AntennaInputs &inputs, QString *error = nullptr);

signals:
    void antennaImpedanceAvailable(double frequencyHz, double resistanceOhm, double reactanceOhm,
                                   double referenceOhm, const QString &feedName);
    void antennaSweepAvailable(const QVector<double> &frequencyHz, const QVector<double> &resistanceOhm,
                               const QVector<double> &reactanceOhm, double referenceOhm,
                               const QString &feedName);

private:
    struct WireElement
    {
        QString name;
        QPointF aM;
        QPointF bM;
        double radiusM = 0.001;
        double azM = 0.0;
        double bzM = 0.0;
        QString id;
    };

    struct PlaneElement
    {
        QString name;
        QPointF centerM;
        double zM = 0.0;
        int surfaceType = 0; // 0=rectangle, 1=disk, 2=cylinder, 3=cone, 4=paraboloid
        int orientation = 0; // base frame: 0=XY, 1=XZ, 2=YZ
        double yawDeg = 0.0;
        double pitchDeg = 0.0;
        double rollDeg = 0.0;
        double radiusM = 0.25;
        double innerRadiusM = 0.0;
        double widthM = 1.0;
        double heightM = 1.0;
        double focalLengthM = 0.25;
        double feedWidthM = 0.003;
        double feedLengthM = 0.020;
        double insetDepthM = 0.0;
        double notchGapM = 0.0005;
        double meshHintM = 0.05;
        QString id;

        AntennaSurface::SurfaceSpec toSurfaceSpec() const
        {
            AntennaSurface::SurfaceSpec s;
            s.name=name.toStdString();
            s.kind=static_cast<AntennaSurface::SurfaceKind>(surfaceType);
            s.center={centerM.x(),centerM.y(),zM};
            s.baseOrientation=orientation; s.yawDeg=yawDeg; s.pitchDeg=pitchDeg; s.rollDeg=rollDeg;
            s.radiusM=radiusM; s.innerRadiusM=innerRadiusM; s.widthM=widthM; s.heightM=heightM;
            s.focalLengthM=focalLengthM; s.feedWidthM=feedWidthM; s.feedLengthM=feedLengthM;
            s.insetDepthM=insetDepthM; s.notchGapM=notchGapM; s.meshHintM=meshHintM;
            return s;
        }
    };

    struct DielectricElement
    {
        QString name;
        QPointF centerM;
        double zM = 0.0;
        int orientation = 0;
        double yawDeg = 0.0;
        double pitchDeg = 0.0;
        double rollDeg = 0.0;
        double widthM = 0.10;
        double heightM = 0.10;
        double thicknessM = 0.0016;
        double relativePermittivity = 4.2;
        double lossTangent = 0.02;
        double fieldFillFactor = 0.65;
        QString id;
    };

    struct FeedPoint
    {
        QString name;
        QPointF positionM;
        double voltageV = 1.0;
        double phaseDeg = 0.0;
        double sourceOhm = 50.0;
        double zM = 0.0;
        QString id;
    };

    struct GeometryPoint
    {
        QPointF xyM;
        double zM = 0.0;
    };

    struct ConstraintRef
    {
        int kind = 0; // 0=wire, 1=feed, 2=PEC surface, 3=dielectric
        QString id;
        int anchor = 0; // wire only: 0=center, 1=endpoint A, 2=endpoint B
    };

    struct GeometryConstraint
    {
        QString id;
        QString name;
        int type = 0; // 0=coincident, 1=parallel wires, 2=perpendicular wires, 3=equal wire length, 4=point distance, 5=equal W/H, 6=wire angle, 7=fixed wire length, 8=concentric PEC, 9=wire parallel surface, 10=wire normal surface, 11=point-surface distance, 12=wire tangent to PEC disk, 13=fix reference position axes
        ConstraintRef a;
        ConstraintRef b;
        double valueM = 0.0;
        double valueDeg = 0.0;
        bool enabled = true;
        int solveMode = 0; // 0=A reference/B driven, 1=balanced when the constraint supports it
        int fixedMask = 7; // type 13: bit0=X, bit1=Y, bit2=Z
        GeometryPoint fixedPoint;
    };

    struct GeometryGroup
    {
        QString id;
        QString name;
        std::vector<QString> objectIds;
    };

    struct GeometrySnapshot
    {
        std::vector<WireElement> wires;
        std::vector<FeedPoint> feeds;
        std::vector<PlaneElement> planes;
        std::vector<DielectricElement> dielectrics;
        std::vector<GeometryConstraint> constraints;
        std::vector<GeometryGroup> groups;
    };

    class Canvas;
    class Geometry3DView;
    class SmithChart;
    class PolarPatternPlot;
    class RadiationPattern3D;

    void rebuildScene(bool fitView = false);
    void refreshTables();
    void refreshSummary();
    void addWire(const QPointF &aM, const QPointF &bM);
    void addFeed(const QPointF &positionM);
    void deleteNearest(const QPointF &positionM);
    void moveNode(const QPointF &oldProjectedM, double oldHiddenM, const QPointF &newProjectedM);
    void addWire3D(const GeometryPoint &a, const GeometryPoint &b);
    void addFeed3D(const GeometryPoint &position);
    void addRectPec3D(const GeometryPoint &a, const GeometryPoint &b);
    void addDielectricSlab3D(const GeometryPoint &a, const GeometryPoint &b,
                             double thicknessM, double relativePermittivity);
    void resizeWire3D(int wireIndex, double lengthM, double radiusM);
    void updateFeedExcitation3D(int feedIndex, double voltageV, double phaseDeg, double referenceOhm);
    void resizeRectPec3D(int planeIndex, const GeometryPoint &center, double widthM, double heightM);
    void resizeDielectric3D(int dielectricIndex, const GeometryPoint &center, double widthM, double heightM);
    void resizeDielectricThickness3D(int dielectricIndex, const GeometryPoint &center, double thicknessM);
    void resizePlaneRadius3D(int planeIndex, double radiusM);
    void coincideSelectedSurfaceCenters3D();
    void matchSelectedSurfaceDimensions3D();
    void ensureGeometryIds();
    void pruneInvalidGeometryConstraints();
    void pruneInvalidGeometryGroups();
    void refreshConstraintEditor();
    void refreshGroupTable();
    void createGroupFrom3DSelection();
    void ungroupSelectedGeometryGroups();
    void selectGeometryGroupMembers();
    void expandSelectionByGroups(std::set<int> &wireIndices, std::set<int> &feedIndices,
                                 std::set<int> &planeIndices, std::set<int> &dielectricIndices) const;
    bool selectionTouchesGeometryGroup(const std::vector<int> &wireIndices, const std::vector<int> &feedIndices,
                                       const std::vector<int> &planeIndices, const std::vector<int> &dielectricIndices) const;
    void addGeometryConstraint();
    void removeSelectedGeometryConstraints();
    void toggleSelectedGeometryConstraints();
    void editGeometryConstraintValue(int constraintIndex);
    void removeGeometryConstraintIndex(int constraintIndex);
    void selectGeometryConstraint(int constraintIndex);
    void solveGeometryConstraints(bool updateStatus = true);
    QString constraintObjectLabel(const ConstraintRef &ref) const;
    GeometryPoint constraintReferencePoint(const ConstraintRef &ref) const;
    double geometryConstraintResidual(const GeometryConstraint &constraint) const;
    QString geometryConstraintDiagnostic(int constraintIndex) const;
    QString geometryConstraintAnalysisText() const;
    void deleteNearest3D(const GeometryPoint &position);
    void deleteGeometrySelection3D(const std::vector<int> &wireIndices, const std::vector<int> &feedIndices,
                                   const std::vector<int> &planeIndices, const std::vector<int> &dielectricIndices,
                                   bool selectedNode, const GeometryPoint &nodePosition);
    void moveNode3D(const GeometryPoint &oldPosition, const GeometryPoint &newPosition);
    void moveFeed3D(int feedIndex, const GeometryPoint &newPosition);
    void translateWire3D(int wireIndex, const GeometryPoint &delta);
    void translatePlane3D(int planeIndex, const GeometryPoint &delta);
    void translateDielectric3D(int dielectricIndex, const GeometryPoint &delta);
    void translateSelection3D(const std::vector<int> &wireIndices, const std::vector<int> &feedIndices,
                              const std::vector<int> &planeIndices, const std::vector<int> &dielectricIndices,
                              const GeometryPoint &delta);
    void duplicate3DSelection();
    void align3DSelection(int axis);
    void translate3DSelectionNumerically(const GeometryPoint &delta);
    void rotate3DSelection(int axis, double angleDeg, const GeometryPoint &pivot);
    void rotate3DSelectionAroundVector(const GeometryPoint &axis, double angleDeg, const GeometryPoint &pivot);
    void mirrorCopy3DSelection(int axis, const GeometryPoint &pivot);
    GeometryPoint snapPoint3D(const GeometryPoint &point, double toleranceM) const;
    GeometryPoint snapPointToWireGeometry3D(const GeometryPoint &point, double toleranceM, bool splitWire,
                                            const std::set<QString> &ignoredObjectIds = {});
    GeometryPoint snapFeedPoint3D(const GeometryPoint &point, double toleranceM, bool splitWire);
    void splitWiresAtSelectedEndpoints(const std::set<int> &selectedWireIndices);
    void pushGeometryHistory();
    void undoGeometry();
    void redoGeometry();
    GeometrySnapshot geometrySnapshot() const;
    void restoreGeometrySnapshot(const GeometrySnapshot &snapshot);
    void deleteSelectedRows();
    void clearGeometry();
    void generateHalfWaveDipole();
    void generateVDipole();
    void generateFoldedDipole();
    void generateSquareLoop();
    void generateYagiArray();
    void generateHelicalAntenna();
    void generateProbeFedPatch();
    void generateInsetMicrostripPatch();
    void generateQuarterWaveGroundPlane();
    void generateInvertedFAntenna();
    void buildPrimitive();
    void generatePreset(int index);
    void saveGeometry();
    void loadGeometry();
    void solveGeneralizedMom();
    void checkMomConvergence();
    void runPhysicalValidation(bool allCases);
    void solveSurfaceMom();
    void solveHybridMom();
    void solveFrequencySweep();
    bool buildHybridInput(double frequencyHz, bool computeFarField, HybridWireSurfaceMom::Input &input, QString &error) const;
    int hybridResultFeedIndex(const HybridWireSurfaceMom::Result &result, int requestedFeedIndex) const;
    void clearMomResults();
    void clearSweepResults();
    void optimizeGeometry();
    void applyOptimizedGeometry();
    void clearOptimizationResults();
    void optimizeYagiMulti();
    void optimizeYagiIndividual();
    void refreshYagiIndividualVariables();
    void applyMultiOptimizedGeometry();
    void clearMultiOptimizationResults();

    QPointF snapToExistingNode(const QPointF &pM, double toleranceM) const;
    QPointF snapFeedToWire(const QPointF &pM, double toleranceM) const;

    Canvas *m_canvas = nullptr;
    QTabWidget *m_workspaceTabs = nullptr;
    Geometry3DView *m_geometry3D = nullptr;
    QComboBox *m_editPlane = nullptr;
    QDoubleSpinBox *m_activePlaneCoordinateM = nullptr;
    double m_constructionPlaneRotXDeg = 0.0;
    double m_constructionPlaneRotYDeg = 0.0;
    double m_constructionPlaneRotZDeg = 0.0;
    QDoubleSpinBox *m_frequencyMHz = nullptr;
    QDoubleSpinBox *m_wireRadiusMm = nullptr;
    bool m_showPhysicalWireRadius = false;
    QDoubleSpinBox *m_gridMm = nullptr;
    QDoubleSpinBox *m_feedVoltageV = nullptr;
    QDoubleSpinBox *m_feedPhaseDeg = nullptr;
    QDoubleSpinBox *m_feedSourceOhm = nullptr;
    QComboBox *m_reportedFeed = nullptr;
    QComboBox *m_presetCombo = nullptr;
    QDoubleSpinBox *m_presetDrivenLambda = nullptr;
    QDoubleSpinBox *m_presetVOpeningDeg = nullptr;
    QDoubleSpinBox *m_presetFoldedSpacingLambda = nullptr;
    QDoubleSpinBox *m_presetLoopPerimeterLambda = nullptr;
    QDoubleSpinBox *m_presetYagiReflectorLambda = nullptr;
    QDoubleSpinBox *m_presetYagiDirectorLambda = nullptr;
    QDoubleSpinBox *m_presetYagiReflectorSpacingLambda = nullptr;
    QDoubleSpinBox *m_presetYagiDirectorSpacingLambda = nullptr;
    QSpinBox *m_presetYagiDirectorCount = nullptr;
    QDoubleSpinBox *m_presetYagiDirectorPitchLambda = nullptr;
    QDoubleSpinBox *m_presetYagiDirectorTaperLambda = nullptr;
    QDoubleSpinBox *m_presetHelixRadiusLambda = nullptr;
    QDoubleSpinBox *m_presetHelixPitchLambda = nullptr;
    QDoubleSpinBox *m_presetHelixTurns = nullptr;
    QDoubleSpinBox *m_presetPatchEr = nullptr;
    QDoubleSpinBox *m_presetPatchHeightMm = nullptr;
    QDoubleSpinBox *m_presetPatchTanD = nullptr;
    QDoubleSpinBox *m_presetPatchLineZ0 = nullptr;
    QDoubleSpinBox *m_presetPatchEdgeResistance = nullptr;
    QDoubleSpinBox *m_presetPatchNotchGapMm = nullptr;
    QLabel *m_presetPatchEstimate = nullptr;
    QComboBox *m_primitiveType = nullptr;
    QComboBox *m_primitiveOrientation = nullptr;
    QDoubleSpinBox *m_primitiveOriginX = nullptr;
    QDoubleSpinBox *m_primitiveOriginY = nullptr;
    QDoubleSpinBox *m_primitiveOriginZ = nullptr;
    QDoubleSpinBox *m_primitiveYawDeg = nullptr;
    QDoubleSpinBox *m_primitivePitchDeg = nullptr;
    QDoubleSpinBox *m_primitiveRollDeg = nullptr;
    QDoubleSpinBox *m_primitiveRadiusM = nullptr;
    QDoubleSpinBox *m_primitiveInnerRadiusM = nullptr;
    QDoubleSpinBox *m_primitiveWidthM = nullptr;
    QDoubleSpinBox *m_primitiveHeightM = nullptr;
    QDoubleSpinBox *m_primitiveFocalLengthM = nullptr;
    QDoubleSpinBox *m_primitivePitchM = nullptr;
    QDoubleSpinBox *m_primitiveThicknessM = nullptr;
    QDoubleSpinBox *m_primitiveDielectricEr = nullptr;
    QDoubleSpinBox *m_primitiveDielectricTanD = nullptr;
    QDoubleSpinBox *m_primitiveDielectricFill = nullptr;
    QDoubleSpinBox *m_primitiveTurns = nullptr;
    QDoubleSpinBox *m_primitiveStartDeg = nullptr;
    QDoubleSpinBox *m_primitiveSweepDeg = nullptr;
    QDoubleSpinBox *m_primitiveGridSpacingM = nullptr;
    QSpinBox *m_primitiveSegments = nullptr;
    QCheckBox *m_primitiveAddFeed = nullptr;
    QCheckBox *m_primitiveClearFirst = nullptr;
    QLabel *m_primitiveHelp = nullptr;
    QTableWidget *m_wireTable = nullptr;
    QTableWidget *m_feedTable = nullptr;
    QTableWidget *m_feedSimulationTable = nullptr;
    QTableWidget *m_planeTable = nullptr;
    QTableWidget *m_dielectricTable = nullptr;
    QTableWidget *m_groupTable = nullptr;
    QComboBox *m_constraintType = nullptr;
    QComboBox *m_constraintRefA = nullptr;
    QComboBox *m_constraintRefB = nullptr;
    QDoubleSpinBox *m_constraintValueMm = nullptr;
    QComboBox *m_constraintSolveMode = nullptr;
    QComboBox *m_constraintFixAxes = nullptr;
    QTableWidget *m_constraintTable = nullptr;
    QTableWidget *m_constraintDofTable = nullptr;
    QLabel *m_constraintStatus = nullptr;
    QLabel *m_constraintGraphStatus = nullptr;
    QLabel *m_summary = nullptr;
    QSpinBox *m_segmentsPerWavelength = nullptr;
    QSpinBox *m_maxMomUnknowns = nullptr;
    QDoubleSpinBox *m_radiusMeshFactor = nullptr;
    QSpinBox *m_junctionLocalSubdivisions = nullptr;
    QComboBox *m_junctionTreatment = nullptr;
    QComboBox *m_currentBasisTreatment = nullptr;
    QTableWidget *m_momConvergenceTable = nullptr;
    QLabel *m_momConvergenceStatus = nullptr;
    QComboBox *m_validationCase = nullptr;
    QTableWidget *m_validationTable = nullptr;
    QLabel *m_validationStatus = nullptr;
    QComboBox *m_surfaceExcitationMode = nullptr;
    QComboBox *m_surfacePortFeed = nullptr;
    QSpinBox *m_surfaceMaxTriangles = nullptr;
    QSpinBox *m_surfaceMaxUnknowns = nullptr;
    QDoubleSpinBox *m_surfaceIncidenceAzDeg = nullptr;
    QDoubleSpinBox *m_surfaceIncidenceElDeg = nullptr;
    QDoubleSpinBox *m_surfacePolarizationDeg = nullptr;
    QDoubleSpinBox *m_surfaceFieldVpm = nullptr;
    QDoubleSpinBox *m_surfaceSelfRegularization = nullptr;
    QSpinBox *m_hybridMaxUnknowns = nullptr;
    QDoubleSpinBox *m_hybridMutualRegularization = nullptr;
    QComboBox *m_hybridPortMode = nullptr;
    QComboBox *m_hybridPortFeed = nullptr;
    QComboBox *m_hybridPortSurface = nullptr;
    QComboBox *m_hybridJunctionFeed = nullptr;
    QComboBox *m_hybridJunctionSurface = nullptr;
    QDoubleSpinBox *m_hybridMappingToleranceMm = nullptr;
    QDoubleSpinBox *m_hybridDifferentialPortRadiusMm = nullptr;
    QCheckBox *m_hybridUseDielectric = nullptr;
    QComboBox *m_hybridDielectricKernel = nullptr;
    QCheckBox *m_hybridFiniteConductivity = nullptr;
    QDoubleSpinBox *m_hybridSurfaceConductivityMSm = nullptr;
    QDoubleSpinBox *m_hybridSurfaceThicknessUm = nullptr;
    QCheckBox *m_hybridTerminalHalfRwg = nullptr;
    QComboBox *m_hybridPortReferenceModel = nullptr;
    QDoubleSpinBox *m_hybridCoaxInnerRadiusMm = nullptr;
    QDoubleSpinBox *m_hybridCoaxOuterRadiusMm = nullptr;
    QDoubleSpinBox *m_hybridCoaxEr = nullptr;
    QDoubleSpinBox *m_hybridCoaxTanD = nullptr;
    QDoubleSpinBox *m_hybridCoaxLengthMm = nullptr;
    QLabel *m_surfaceMomStatus = nullptr;
    QLabel *m_surfaceMomMetrics = nullptr;
    QLabel *m_hybridMomStatus = nullptr;
    QLabel *m_hybridMomMetrics = nullptr;
    QDoubleSpinBox *m_sweepStartMHz = nullptr;
    QDoubleSpinBox *m_sweepStopMHz = nullptr;
    QSpinBox *m_sweepPoints = nullptr;
    QComboBox *m_sweepFeed = nullptr;
    QComboBox *m_sweepExcitationMode = nullptr;
    QComboBox *m_sweepSolver = nullptr;
    QDoubleSpinBox *m_optTargetMHz = nullptr;
    QComboBox *m_optFeed = nullptr;
    QComboBox *m_optVariable = nullptr;
    QComboBox *m_optObjective = nullptr;
    QDoubleSpinBox *m_optMinFactor = nullptr;
    QDoubleSpinBox *m_optMaxFactor = nullptr;
    QSpinBox *m_optCoarseSamples = nullptr;
    QSpinBox *m_optRefineIterations = nullptr;
    QLabel *m_optStatus = nullptr;
    QLabel *m_optSummary = nullptr;
    QPushButton *m_applyOptimized = nullptr;
    QDoubleSpinBox *m_multiTargetMHz = nullptr;
    QComboBox *m_multiFeed = nullptr;
    QCheckBox *m_multiDrivenLength = nullptr;
    QCheckBox *m_multiReflectorLength = nullptr;
    QCheckBox *m_multiDirectorLength = nullptr;
    QCheckBox *m_multiReflectorSpacing = nullptr;
    QCheckBox *m_multiDirectorSpacing = nullptr;
    QDoubleSpinBox *m_multiMinFactor = nullptr;
    QDoubleSpinBox *m_multiMaxFactor = nullptr;
    QSpinBox *m_multiSamplesPerVariable = nullptr;
    QSpinBox *m_multiPasses = nullptr;
    QDoubleSpinBox *m_multiWeightMatch = nullptr;
    QDoubleSpinBox *m_multiWeightDirectivity = nullptr;
    QDoubleSpinBox *m_multiWeightFrontBack = nullptr;
    QDoubleSpinBox *m_multiWeightBandwidth = nullptr;
    QDoubleSpinBox *m_multiBandwidthHalfSpanPct = nullptr;
    QSpinBox *m_multiBandwidthSamples = nullptr;
    QComboBox *m_multiBandwidthAggregation = nullptr;
    QDoubleSpinBox *m_multiMinElementSpacingLambda = nullptr;
    QDoubleSpinBox *m_multiMaxBoomLengthLambda = nullptr;
    QLabel *m_multiStatus = nullptr;
    QLabel *m_multiSummary = nullptr;
    QPushButton *m_applyMultiOptimized = nullptr;
    QTableWidget *m_individualDirectorTable = nullptr;
    QLabel *m_individualStatus = nullptr;
    QLabel *m_individualSummary = nullptr;
    QPushButton *m_applyIndividualOptimized = nullptr;
    QLabel *m_momStatus = nullptr;
    QLabel *m_momFeedResults = nullptr;
    QLabel *m_momMetrics = nullptr;
    QLabel *m_sweepStatus = nullptr;
    QLabel *m_sweepSummary = nullptr;
    QTabWidget *m_resultTabs = nullptr;
    FieldProfilePlot *m_currentMagnitudePlot = nullptr;
    FieldProfilePlot *m_surfaceCurrentPlot = nullptr;
    FieldProfilePlot *m_surfaceRcsPlot = nullptr;
    FieldProfilePlot *m_currentPhasePlot = nullptr;
    FieldProfilePlot *m_chargeMagnitudePlot = nullptr;
    FieldProfilePlot *m_validationPatternPlot = nullptr;
    FieldProfilePlot *m_azimuthPlot = nullptr;
    FieldProfilePlot *m_elevationPlot = nullptr;
    PolarPatternPlot *m_azimuthPolarPlot = nullptr;
    PolarPatternPlot *m_elevationPolarPlot = nullptr;
    RadiationPattern3D *m_radiation3D = nullptr;
    FieldProfilePlot *m_sweepZPlot = nullptr;
    FieldProfilePlot *m_sweepS11Plot = nullptr;
    FieldProfilePlot *m_sweepVswrPlot = nullptr;
    SmithChart *m_sweepSmith = nullptr;
    FieldProfilePlot *m_optimizationPlot = nullptr;
    FieldProfilePlot *m_multiOptimizationPlot = nullptr;
    FieldProfilePlot *m_individualOptimizationPlot = nullptr;

    std::vector<WireElement> m_wires;
    std::vector<FeedPoint> m_feeds;
    std::vector<PlaneElement> m_planes;
    std::vector<DielectricElement> m_dielectrics;
    std::vector<GeometryConstraint> m_constraints;
    std::vector<GeometryGroup> m_groups;
    bool m_magneticSnapEnabled = false;
    std::vector<GeometrySnapshot> m_undoHistory;
    std::vector<GeometrySnapshot> m_redoHistory;
    bool m_restoringGeometryHistory = false;
    bool m_applyingGeometryConstraints = false;
    std::vector<WireElement> m_optimizedWires;
    std::vector<FeedPoint> m_optimizedFeeds;
    std::vector<WireElement> m_multiOptimizedWires;
    std::vector<FeedPoint> m_multiOptimizedFeeds;
    std::vector<WireElement> m_individualOptimizedWires;
    std::vector<FeedPoint> m_individualOptimizedFeeds;
};
