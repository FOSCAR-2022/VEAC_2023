#include "headerFile/header.h"
#include "headerFile/dbscan.h"

using namespace std;

int minPoints;
double epsilon;

int minClusterSize;
int maxClusterSize;

double xMinROI;
double xMaxROI;
double yMinROI;
double yMaxROI;
double zMinROI;
double zMaxROI;

double xMinBoundingBox;
double xMaxBoundingBox;
double yMinBoundingBox;
double yMaxBoundingBox;
double zMinBoundingBox;
double zMaxBoundingBox;

typedef pcl::PointXYZ PointT;

vector<float> obstacle;
vector< vector<float> > obstacle_vec;

ros::Publisher trObsClusterPub; //Cluster Publishser
ros::Publisher trObsMarkerPub; //Bounnding Box Visualization Publisher
ros::Publisher trObsCropboxPub; //Cropbox Publishser
ros::Publisher trObsPositionPub; //Bounding Box Position Publisher


laser_geometry::LaserProjection projector_;
// tf::TransformListener listener_;

void dynamicParamCallback(obstacle_detection::tr_hyper_parameterConfig &config, int32_t level) {
  minPoints = config.minPoints;
  epsilon = config.epsilon;
  minClusterSize = config.minClusterSize;
  maxClusterSize = config.maxClusterSize;

  xMinROI = config.xMinROI;
  xMaxROI = config.xMaxROI;
  yMinROI = config.yMinROI;
  yMaxROI = config.yMaxROI;
  zMinROI = config.zMinROI;
  zMaxROI = config.zMaxROI;

  xMinBoundingBox = config.xMinBoundingBox;
  xMaxBoundingBox = config.xMaxBoundingBox;
  yMinBoundingBox = config.yMinBoundingBox;
  yMaxBoundingBox = config.yMaxBoundingBox;
  zMinBoundingBox = config.zMinBoundingBox;
  zMaxBoundingBox = config.zMaxBoundingBox;
}

void cloud_cb(const sensor_msgs::PointCloud2 inputcloud) {
  //ROS message 변환
  //PointXYZI가 아닌 PointXYZ로 선언하는 이유 -> 각각의 Cluster를 다른 색으로 표현해주기 위해서. Clustering 이후 각각 구별되는 intensity value를 넣어줄 예정.
  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZ>);
  pcl::fromROSMsg(inputcloud, *cloud);

  //Visualizing에 필요한 Marker 선언
  visualization_msgs::MarkerArray BoxArray;
  visualization_msgs::Marker Box;

  //Boundingbox & Waypoitn Position Messsage 
  obstacle_detection::ObjectInfo ObjectInfo;

  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_xf(new pcl::PointCloud<pcl::PointXYZ>);
  pcl::PassThrough<pcl::PointXYZ> xfilter;
  xfilter.setInputCloud(cloud);
  xfilter.setFilterFieldName("x");
  xfilter.setFilterLimits(xMinROI, xMaxROI); 
  xfilter.setFilterLimitsNegative(false);
  xfilter.filter(*cloud_xf);

  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_xyf(new pcl::PointCloud<pcl::PointXYZ>);
  pcl::PassThrough<pcl::PointXYZ> yfilter;
  yfilter.setInputCloud(cloud_xf);
  yfilter.setFilterFieldName("y");
  yfilter.setFilterLimits(yMinROI, yMaxROI);
  yfilter.setFilterLimitsNegative(false);
  yfilter.filter(*cloud_xyf);

  pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_xyzf(new pcl::PointCloud<pcl::PointXYZ>);
  pcl::PassThrough<pcl::PointXYZ> zfilter;
  zfilter.setInputCloud(cloud_xyf);
  zfilter.setFilterFieldName("z");
  zfilter.setFilterLimits(zMinROI, zMaxROI);
  zfilter.setFilterLimitsNegative(false);
  zfilter.filter(*cloud_xyzf);

  //KD-Tree
  pcl::search::KdTree<pcl::PointXYZ>::Ptr tree(new pcl::search::KdTree<pcl::PointXYZ>);
  if (cloud_xyzf->size() > 0) {
    tree->setInputCloud(cloud_xyzf);
  }
  //Segmentation
  vector<pcl::PointIndices> cluster_indices;
  
  //DBSCAN with Kdtree for accelerating
  DBSCANKdtreeCluster<pcl::PointXYZ> dc;
  dc.setCorePointMinPts(minPoints);   //Set minimum number of neighbor points
  dc.setClusterTolerance(epsilon); //Set Epsilon 
  dc.setMinClusterSize(minClusterSize);
  dc.setMaxClusterSize(maxClusterSize);
  dc.setSearchMethod(tree);
  dc.setInputCloud(cloud_xyzf);
  dc.extract(cluster_indices);

  pcl::PointCloud<pcl::PointXYZI> totalcloud_clustered;
  int cluster_id = 0;

  //각 Cluster 접근
  for (vector<pcl::PointIndices>::const_iterator it = cluster_indices.begin(); it != cluster_indices.end(); it++, cluster_id++) {
    pcl::PointCloud<pcl::PointXYZI> eachcloud_clustered;
    float cluster_counts = cluster_indices.size();
    //각 Cluster내 각 Point 접근
    for(vector<int>::const_iterator pit = it->indices.begin(); pit != it->indices.end(); ++pit) {
        pcl::PointXYZI tmp;
        tmp.x = cloud_xyzf->points[*pit].x;
        tmp.y = cloud_xyzf->points[*pit].y;
        tmp.z = cloud_xyzf->points[*pit].z;
        tmp.intensity = cluster_id%100;
        eachcloud_clustered.push_back(tmp);
        totalcloud_clustered.push_back(tmp);
    }

    //minPoint와 maxPoint 받아오기
    pcl::PointXYZI minPoint, maxPoint;
    pcl::getMinMax3D(eachcloud_clustered, minPoint, maxPoint);

    float x_len = abs(maxPoint.x - minPoint.x);   //직육면체 x 모서리 크기
    float y_len = abs(maxPoint.y - minPoint.y);   //직육면체 y 모서리 크기
    float z_len = abs(maxPoint.z - minPoint.z);   //직육면체 z 모서리 크기 

    float volume = x_len * y_len * z_len;         //직육면체 부피

    float center_x = (minPoint.x + maxPoint.x)/2; //직육면체 중심 x 좌표
    float center_y = (minPoint.y + maxPoint.y)/2; //직육면체 중심 y 좌표
    float center_z = (minPoint.z + maxPoint.z)/2; //직육면체 중심 z 좌표 

    float distance = sqrt(center_x * center_x + center_y * center_y); //장애물 <-> 차량 거리

    ObjectInfo.lengthX[cluster_id] = x_len;
    ObjectInfo.lengthY[cluster_id] = y_len;
    ObjectInfo.lengthZ[cluster_id] = z_len;
    ObjectInfo.centerX[cluster_id] = center_x;
    ObjectInfo.centerY[cluster_id] = center_y;
    ObjectInfo.centerZ[cluster_id] = center_z;

    if ( (xMinBoundingBox <= x_len && x_len <= xMaxBoundingBox) && (yMinBoundingBox <= y_len && y_len <= yMaxBoundingBox) && (zMinBoundingBox <= z_len && z_len <= zMaxBoundingBox) ) {
      Box.header.frame_id = "velodyne";
      Box.header.stamp = ros::Time();
      Box.ns = cluster_counts; //ns = namespace
      Box.id = cluster_id; 
      Box.type = visualization_msgs::Marker::CYLINDER; 
      Box.action = visualization_msgs::Marker::ADD;

      Box.pose.position.x = center_x; 
      Box.pose.position.y = center_y;
      Box.pose.position.z = center_z;

      Box.pose.orientation.x = 0.0;
      Box.pose.orientation.y = 0.0;
      Box.pose.orientation.z = 0.0;
      Box.pose.orientation.w = 1.0;
      
      float scale;
      if (x_len >= y_len) scale = x_len;
      else scale = y_len;
      
      Box.scale.x = scale;
      Box.scale.y = scale;
      Box.scale.z = 0.01;

      Box.color.a = 0.8; //직육면체 투명도, a = alpha
      Box.color.r = 0.0; //직육면체 색상 RGB값
      Box.color.g = 1.0;
      Box.color.b = 0.0;

      Box.lifetime = ros::Duration(0.1); //box 지속시간
      BoxArray.markers.emplace_back(Box);
    }
    // cluster_id++; //intensity 증가
    ObjectInfo.objectCounts = cluster_id+1;
  }


  //Convert To ROS data type 
  pcl::PCLPointCloud2 cloud_p;
  pcl::toPCLPointCloud2(totalcloud_clustered, cloud_p);

  sensor_msgs::PointCloud2 cluster;
  pcl_conversions::fromPCL(cloud_p, cluster);
  cluster.header.frame_id = "velodyne";

  pcl::PCLPointCloud2 cloud_cropbox;
  pcl::toPCLPointCloud2(*cloud_xyzf, cloud_cropbox);

  sensor_msgs::PointCloud2 cropbox;
  pcl_conversions::fromPCL(cloud_cropbox, cropbox);
  cropbox.header.frame_id = "velodyne";

  trObsClusterPub.publish(cluster);
  trObsMarkerPub.publish(BoxArray);
  trObsCropboxPub.publish(cropbox);
  trObsPositionPub.publish(ObjectInfo);

  vector< vector<float> >().swap(obstacle_vec);   
}

void laser2cloudmsg (const sensor_msgs::LaserScan::ConstPtr& scan_in) {
  sensor_msgs::PointCloud2 pc2_dst;
  projector_.projectLaser(*scan_in, pc2_dst);

  cloud_cb(pc2_dst);
}

int main(int argc, char **argv) {
  ros::init(argc, argv, "track_obstacle");
  ros::NodeHandle nh;

  dynamic_reconfigure::Server<obstacle_detection::tr_hyper_parameterConfig> server;
  dynamic_reconfigure::Server<obstacle_detection::tr_hyper_parameterConfig>::CallbackType f;

  f = boost::bind(&dynamicParamCallback, _1, _2);
  server.setCallback(f);

  ros::Subscriber rawDataSub = nh.subscribe("/lidar2D", 1, laser2cloudmsg);  // velodyne_points 토픽 구독. velodyne_points = 라이다 raw data

  trObsClusterPub = nh.advertise<sensor_msgs::PointCloud2>("/tr_obs_cluster", 0.001);                  
  trObsMarkerPub = nh.advertise<visualization_msgs::MarkerArray>("/tr_obs_marker", 0.001);     
  trObsCropboxPub = nh.advertise<sensor_msgs::PointCloud2>("/tr_obs_cropbox", 0.001); 
  trObsPositionPub = nh.advertise<obstacle_detection::ObjectInfo>("/tr_obs_position", 0.001); 

  ros::spin();

  return 0;
}
